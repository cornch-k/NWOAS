#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p out
/usr/bin/clang -std=c11 -Wall -Wextra -Werror -O2 -dynamiclib admin_payload.c -o out/payload.dylib
python3 test.py
python3 test_profiles.py
/opt/homebrew/opt/llvm/bin/clang --target=aarch64-none-elf -ffreestanding -fno-builtin -std=c11 -Wall -Wextra -Werror -O2 -c admin_payload.c -o out/payload-arm64.o
/opt/homebrew/opt/llvm/bin/llvm-nm --undefined-only out/payload-arm64.o > out/undefined.txt
test ! -s out/undefined.txt
/usr/bin/clang -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all admin_payload.c test_bounds.c -o out/test-bounds
./out/test-bounds
echo 'PASS sanitizers and API/canary boundary tests'
