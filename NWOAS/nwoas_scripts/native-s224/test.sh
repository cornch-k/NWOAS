#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p out
/usr/bin/clang -std=c11 -Wall -Wextra -Werror -O2 -dynamiclib test_bridge.c -o out/projection.dylib
python3 test_differential.py
python3 test_module.py
python3 test_owner.py
/usr/bin/clang -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all test_bounds.c -o out/test-bounds
./out/test-bounds
/opt/homebrew/opt/llvm/bin/clang --target=aarch64-none-elf -ffreestanding -fno-builtin -std=c11 -Wall -Wextra -Werror -O2 -c test_bridge.c -o out/projection-arm64.o
/opt/homebrew/opt/llvm/bin/llvm-nm --undefined-only out/projection-arm64.o > out/undefined.txt
test ! -s out/undefined.txt
echo 'PASS sanitizers and freestanding ARM64, no undefined imports'
