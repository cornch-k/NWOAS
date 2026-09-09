#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p out
CC=${CC:-/usr/bin/clang}
flags=(-std=c11 -Wall -Wextra -Werror)
"$CC" "${flags[@]}" -O2 -dynamiclib model.c test_bridge.c -o out/model.dylib
python3 test_differential.py
"$CC" "${flags[@]}" -O2 model.c test_model.c -o out/test-plain
./out/test-plain
"$CC" "${flags[@]}" -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all model.c test_model.c -o out/test-sanitize
perl -e 'alarm 30; exec @ARGV' ./out/test-sanitize
/opt/homebrew/opt/llvm/bin/clang "${flags[@]}" --target=aarch64-none-elf -ffreestanding -fno-builtin -O2 -c model.c -o out/model-aarch64.o
/opt/homebrew/opt/llvm/bin/llvm-nm --undefined-only out/model-aarch64.o > out/undefined.txt
test ! -s out/undefined.txt
echo 'PASS: freestanding AArch64 object has no undefined imports'
