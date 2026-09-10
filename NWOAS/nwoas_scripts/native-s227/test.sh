#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p out
/usr/bin/clang -std=c11 -Wall -Wextra -Werror -O2 -I../native-s225 -I../native-s226 -dynamiclib admin_ring.c ../native-s226/admin_state.c ../native-s225/admin_payload.c test_bridge.c -o out/ring.dylib
python3 test.py
python3 test_extra.py
for part in admin_ring ../native-s226/admin_state ../native-s225/admin_payload; do
 /opt/homebrew/opt/llvm/bin/clang --target=aarch64-none-elf -ffreestanding -fno-builtin -std=c11 -Wall -Wextra -Werror -O2 -I../native-s225 -I../native-s226 -c "$part.c" -o "out/$(basename "$part")-arm64.o"
done
/opt/homebrew/opt/lld/bin/ld.lld -r out/admin_ring-arm64.o out/admin_state-arm64.o out/admin_payload-arm64.o -o out/engine-arm64.o
/opt/homebrew/opt/llvm/bin/llvm-nm --undefined-only out/engine-arm64.o > out/undefined.txt
test ! -s out/undefined.txt
/usr/bin/clang -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -I../native-s225 -I../native-s226 admin_ring.c ../native-s226/admin_state.c ../native-s225/admin_payload.c test_faults.c -o out/test-faults
./out/test-faults
