#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p out
/usr/bin/clang -std=c11 -Wall -Wextra -Werror -O2 -I../native-s225 -dynamiclib admin_state.c ../native-s225/admin_payload.c test_bridge.c -o out/admin.dylib
python3 test.py
python3 test_generic.py
/opt/homebrew/opt/llvm/bin/clang --target=aarch64-none-elf -ffreestanding -fno-builtin -std=c11 -Wall -Wextra -Werror -O2 -I../native-s225 -c admin_state.c -o out/admin-arm64.o
/opt/homebrew/opt/llvm/bin/clang --target=aarch64-none-elf -ffreestanding -fno-builtin -std=c11 -Wall -Wextra -Werror -O2 -c ../native-s225/admin_payload.c -o out/payload-arm64.o
/opt/homebrew/opt/lld/bin/ld.lld -r out/admin-arm64.o out/payload-arm64.o -o out/frontend-arm64.o
/opt/homebrew/opt/llvm/bin/llvm-nm --undefined-only out/frontend-arm64.o > out/undefined.txt
test ! -s out/undefined.txt
/usr/bin/clang -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -I../native-s225 admin_state.c ../native-s225/admin_payload.c test_bounds.c -o out/test-bounds
./out/test-bounds
echo 'PASS 100000 malformed-command/canary cases and freestanding ARM64, no imports'
