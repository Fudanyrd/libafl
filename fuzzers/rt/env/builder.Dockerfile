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
# Integration with Fuzzbench (https://github.com/google/fuzzbench)
#

ARG parent_image
FROM $parent_image

RUN apt-get update && \
    apt-get remove -y llvm-10 && \
    apt-get install -y \
        build-essential lsb-release wget software-properties-common gnupg && \
    wget https://apt.llvm.org/llvm.sh && \
    chmod +x llvm.sh && \
    ./llvm.sh 16 && rm -f llvm.sh && \
    apt-get install -y wget libstdc++5 libtool-bin automake flex bison \
        libglib2.0-dev libpixman-1-dev python3-setuptools unzip \
        apt-utils apt-transport-https ca-certificates joe curl \
        xz-utils tar

# Uninstall old Rust & Install the latest one.
RUN if which rustup; then rustup self uninstall -y; fi && \
    curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs > /rustup.sh && \
    sh /rustup.sh --default-toolchain nightly -y && \
    rm /rustup.sh && /root/.cargo/bin/rustup default nightly

# Download our libafl and pre-built gllvm (extracted to /usr/lib/go/)
# Set CLANG and CLANGPP to control the clang executable used by libafl'c clang wrapper.
RUN git clone https://github.com/Fudanyrd/libafl --branch fast --depth 1 /libafl && \
    git clone https://github.com/nlohmann/json.git --depth 1 /libafl/json && \
    cd / && \
    tar xkf /libafl/bin/gllvm.tar.xz && \
    chmod +x /usr/lib/go/* && \
    cd /libafl && \
    unset CFLAGS CXXFLAGS && \
    export LIBAFL_EDGES_MAP_SIZE=2621440 && \
    cd ./fuzzers/rt && \
    env -i CXX=$CXX CC=$CC PATH="/root/.cargo/bin/:$PATH" \
    CLANG=/usr/lib/go/gclang CLANGPP=/usr/lib/go/gclang++ \
    JSON_PATH=/libafl/json \
    cargo build --profile release --features setcover && \
    cp ./target/release/liblibfuzzer_rt.a /usr/lib/libfuzzer_rt.a && \
    cp ./target/release/libafl_ar /usr/lib/libafl_ar && \
    cp ./target/release/libafl_cc /usr/lib/libafl_cc && \
    cp ./target/release/libafl_cxx /usr/lib/libafl_cxx && \
    cp ./target/release/libafl_libtool /usr/lib/libafl_libtool &&\
    clang-16 $( llvm-config-16 --cxxflags ) -fpic -shared -O1 -g \
    /libafl/fuzzers/rt/scripts/pass.cc -o /usr/lib/dump-cfg-pass.so &&\
    cp ./scripts/build-cfg.sh /usr/lib/build-cfg.sh && chmod +x /usr/lib/build-cfg.sh &&\
    cp ./scripts/gen-graph.py /usr/lib/gen-graph.py && chmod +x /usr/lib/gen-graph.py
