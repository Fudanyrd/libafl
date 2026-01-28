# Copyright 2020 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
"""Integration code for a LibAFL-based fuzzer."""

import os
import shutil
import subprocess
import sys
import time
import yaml

from fuzzers import utils

def find_executable(shortname: str) -> str:
    """Find the given executable in PATH."""
    if shortname[0] == '/':
        # this is an absolute path;
        assert os.path.exists(shortname)
        return shortname

    for path in os.environ.get('PATH', '').split(os.pathsep):
        exe_path = os.path.join(path, shortname)
        if os.path.isfile(exe_path) and os.access(exe_path, os.X_OK):
            return exe_path
    raise FileNotFoundError(f'Executable {shortname} not found in PATH')


def set_executable_env(envname: str, shortname: str):
    relpath = find_executable(shortname)
    os.environ[envname] = relpath


GLLVM_INTSALL_DIR = '/usr/lib/go'
LLVM_VERSION_MAJOR = '16'


def prepare_fuzz_environment():
    """Prepare to fuzz with a LibAFL-based fuzzer."""
    os.environ['ASAN_OPTIONS'] = 'abort_on_error=1:detect_leaks=0:' \
                                 'malloc_context_size=0:symbolize=0:' \
                                 'allocator_may_return_null=1:' \
                                 'detect_odr_violation=0:handle_segv=0:' \
                                 'handle_sigbus=0:handle_abort=0:' \
                                 'handle_sigfpe=0:handle_sigill=0'
    os.environ['UBSAN_OPTIONS'] = 'abort_on_error=1:' \
                                  'allocator_release_to_os_interval_ms=500:' \
                                  'handle_abort=0:handle_segv=0:' \
                                  'handle_sigbus=0:handle_sigfpe=0:' \
                                  'handle_sigill=0:print_stacktrace=0:' \
                                  'symbolize=0:symbolize_inline_frames=0'

    # update $PATH
    paths = os.environ['PATH']
    os.environ['PATH'] = paths + ":" + GLLVM_INTSALL_DIR


def prepare_build_environment():
    init_kvs = [
        # most of these are required by gllvm.
        ('LLVM_AR', 'llvm-ar-' + LLVM_VERSION_MAJOR),
        ('LLVM_AR_NAME', 'llvm-ar-' + LLVM_VERSION_MAJOR),
        ('LLVM_LINK_NAME', 'llvm-link-' + LLVM_VERSION_MAJOR),
        ('OPT', 'opt-' + LLVM_VERSION_MAJOR),
        ('GET_BC', os.path.join(GLLVM_INTSALL_DIR, 'get-bc')),
        ('LLVM_CC_NAME', 'clang-' + LLVM_VERSION_MAJOR),
        ('LLVM_CXX_NAME', 'clang++-' + LLVM_VERSION_MAJOR),
        ('LLVM_DIS', 'llvm-dis-' + LLVM_VERSION_MAJOR),
    ]
    for kv in init_kvs:
        set_executable_env(kv[0], kv[1])
    
    # update $PATH
    paths = os.environ['PATH']
    os.environ['PATH'] = paths + ":" + GLLVM_INTSALL_DIR

    # misc
    os.environ['CLANG'] = os.path.join(GLLVM_INTSALL_DIR, 'gclang')
    os.environ['CLANGPP'] = os.path.join(GLLVM_INTSALL_DIR, 'gclang++')


def _handle_xml2() -> None:
    """Xml2 requires -lzma, fix this."""
    cxx: str = 'clang++-' + LLVM_VERSION_MAJOR
    
    # Just hard-code everything.
    subprocess.check_call([
        os.path.join(GLLVM_INTSALL_DIR, "get-bc"),
        "xml"])
    subprocess.check_call([cxx, 'xml.bc', '-c', '-o', 'xml.o'])
    subprocess.check_call(
        [cxx, 'xml.o', '-o', 'xml',
         '-fsanitize=address', 
         '-fsanitize-coverage=trace-pc-guard,pc-table,no-prune',
         '/usr/lib/libfuzzer_rt.a',
         '-lm',
         '-lz',
         '-lrt',
         '-ldl',
         '-lzma'])
    os.unlink("xml.o")
    os.unlink("xml.bc")


_BENCHARK_HANDLERS = {
    "libxml2_xml_e85b9b": _handle_xml2,
    "libxml2_xml": _handle_xml2,
}

def build():
    """Build benchmark."""
    # With LibFuzzer we use -fsanitize=fuzzer-no-link for build CFLAGS and then
    # /usr/lib/libFuzzer.a as the FUZZER_LIB for the main fuzzing binary. This
    # allows us to link against a version of LibFuzzer that we specify.
    cflags = ['-fsanitize=address',
              '-fsanitize-coverage=trace-pc-guard,pc-table,no-prune']
    utils.append_flags('CFLAGS', cflags)
    utils.append_flags('CXXFLAGS', cflags)
    utils.append_flags('CXXFLAGS', ['-stdlib=libstdc++'])

    prepare_build_environment()
    os.environ['CC'] = '/usr/lib/libafl_cc'
    os.environ['CXX'] = '/usr/lib/libafl_cxx'
    os.environ['CCC'] = '/usr/lib/libafl_cxx'
    os.environ['AR'] = '/usr/lib/libafl_ar'
    libfuzz = '/usr/lib/libfuzzer_rt.a'

    # merge all of our lib into a single .o, then pack that into a static lib
    subprocess.check_call([
        '/usr/bin/ld', '-Ur', '--whole-archive', libfuzz, '-o',
        '/tmp/libFuzzerMerged.o'
    ])
    subprocess.check_call(['/usr/bin/rm', libfuzz])
    subprocess.check_call(
        ['/usr/bin/ar', 'cr', libfuzz, '/tmp/libFuzzerMerged.o'])

    os.environ['AR'] = '/usr/lib/libafl_ar'
    os.environ['FUZZER_LIB'] = libfuzz # the same as builder.Dockerfile

    # You should copy any fuzzer binaries that you need at runtime to the
    # $OUT directory. E.g. for AFL:
    # shutil.copy('/afl/afl-fuzz', os.environ['OUT'])

    utils.build_benchmark()

    # Generate global CFG
    fuzz_target_exe: str = utils.get_config_value('fuzz_target')
    cwd = os.getcwd()
    os.chdir(os.environ['OUT'])
    subprocess.check_call(['/usr/lib/build-cfg.sh', fuzz_target_exe],
                          stderr=open('compile.log', 'w'))
    this_benchmark = os.environ['BENCHMARK']
    if this_benchmark in _BENCHARK_HANDLERS.keys():
        handler = _BENCHARK_HANDLERS[this_benchmark]
        handler()
    os.chdir(cwd)


def fuzz(input_corpus, output_corpus, target_binary):
    """Run fuzzer. Wrapper that uses the defaults when calling
    run_fuzzer."""
    run_fuzzer(input_corpus, output_corpus, target_binary)


def _handle_no_seed_found(dirname: str):
    """Write some pre-defined seeds if none exists;
    to avoid crashing the fuzzer."""    
    if os.listdir(dirname):
        pass
    else:
        default_seeds = ['\x01\x14\x05\x14', 
                         '\x00\x00\x00\x00(.*?)[a-kB-E]+',
                         'foo bar baz']
        for i in range(len(default_seeds)):
            with open(os.path.join(dirname, str(i)), 'w') as fobj:
                fobj.write(default_seeds[i])
        del default_seeds


def run_fuzzer(input_corpus, output_corpus, target_binary, extra_flags=None):
    """Run fuzzer.

    Arguments:
      input_corpus: Directory containing the initial seed corpus for
                    the benchmark.
      output_corpus: Output directory to place the newly generated corpus
                     from fuzzer run.
      target_binary: Absolute path to the fuzz target binary.
    """
    if extra_flags is None:
        extra_flags = []

    # ASAN doesn't play nicely with our signal handling
    # in the future, we will make this more compatible with libfuzzer, but
    # for the initial implementation, we consider this sufficient
    prepare_fuzz_environment()
    _handle_no_seed_found(input_corpus)

    # Seperate out corpus and crash directories as sub-directories of
    # |output_corpus| to avoid conflicts when corpus directory is reloaded.
    crashes_dir = os.path.join(output_corpus, 'crashes')
    output_corpus = os.path.join(output_corpus, 'corpus')
    os.makedirs(crashes_dir)
    os.makedirs(output_corpus)

    report_dir = '/tmp/report-data'
    os.environ['AFL_CFG_PATH'] = os.path.realpath(target_binary) + '_cfg'

    # TODO: the fuzzer appends its log to '$PWD/a.log';
    # create a symbolic link from '$OUT/a.log' to '$PWD/a.log'.
    if not os.path.exists(report_dir):
        os.mkdir(report_dir)
    subprocess.check_call(['ln', '-s', os.path.join(report_dir, 'a.log'),
                                os.path.join(os.getcwd(), 'a.log')])

    # A typical command line arguments for libafl:
    # AFL_CFG_PATH=$PWD/fuzzer_ossfuzz_cfg \
    # ./fuzzer_ossfuzz --cores 0 --input input --output ./output_corpus
    command = [target_binary, 
               '--output', output_corpus, 
               '--input', input_corpus,
               '--cores', '0']
    print('[run_fuzzer] Running command: ' + ' '.join(command))
    subprocess.check_call(command)
