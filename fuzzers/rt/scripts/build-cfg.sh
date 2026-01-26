#!/usr/bin/env bash

set -ex

if [[ x"$1" == x"" ]]; then
  echo "Usage: $0 <binary-name-without-suffix>"
  exit 1
fi

$GET_BC $1

bc="$1.bc"
cfg="$1"_cfg
cfg_pass='/usr/lib/dump-cfg-pass.so'
rm -f $cfg || true
$LLVM_DIS $bc -o "$1.ll"

tmpf=$( mktemp tmp.XXXXXXXX.o )
CFG_OUTPUT_PATH="$cfg" $CLANG \
    -Xclang -load \
    -Xclang $cfg_pass \
    -Xclang "-fpass-plugin=$cfg_pass" \
    -Xclang -opaque-pointers \
    -c "$bc" \
    -o "$tmpf"

rm -f "$tmpf" || true
rm -f "$bc" || true
stat "$cfg" >/dev/null

exit 0
