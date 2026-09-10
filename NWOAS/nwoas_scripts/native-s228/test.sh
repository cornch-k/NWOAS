#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p out
INCS=(-I../native-s209 -I../native-s225 -I../native-s226 -I../native-s227)
SOURCES=(frontend.c ../native-s209/model.c ../native-s227/admin_ring.c ../native-s226/admin_state.c ../native-s225/admin_payload.c)
/usr/bin/clang -std=c11 -Wall -Wextra -Werror -O2 "${INCS[@]}" -dynamiclib "${SOURCES[@]}" test_bridge.c -o out/frontend.dylib
python3 test.py
OBJS=()
for part in "${SOURCES[@]}"; do
 obj="out/$(basename "${part%.c}")-arm64.o";OBJS+=("$obj")
 /opt/homebrew/opt/llvm/bin/clang --target=aarch64-none-elf -ffreestanding -fno-builtin -std=c11 -Wall -Wextra -Werror -O2 "${INCS[@]}" -c "$part" -o "$obj"
done
/opt/homebrew/opt/lld/bin/ld.lld -r "${OBJS[@]}" -o out/frontend-combined.o
/opt/homebrew/opt/llvm/bin/llvm-nm --undefined-only out/frontend-combined.o > out/undefined.txt
test ! -s out/undefined.txt
/usr/bin/clang -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all "${INCS[@]}" "${SOURCES[@]}" test_faults.c -o out/test-faults
./out/test-faults
