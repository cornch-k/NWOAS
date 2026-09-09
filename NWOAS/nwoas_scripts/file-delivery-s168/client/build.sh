#!/bin/sh
# Build the S168 no-CRT ARM64 Windows delivery client, mirroring
# native-link-s125/build.sh: freestanding, no default libs, own minimal
# kernel32 import library, zero warnings (-Wall -Wextra -Werror).
set -eu
cd "$(dirname "$0")"
LLVM=${NWOAS_LLVM:-/opt/homebrew/opt/llvm/bin}
tmp=$(mktemp -d /tmp/nwoas-s168-build.XXXXXX)
trap 'rm -f "$tmp/kernel32.lib" "$tmp/nwoas_client.obj"; rmdir "$tmp"' EXIT
"$LLVM/llvm-dlltool" -m arm64 -d kernel32.def -l "$tmp/kernel32.lib"
"$LLVM/clang" --target=aarch64-pc-windows-msvc -O2 -ffreestanding -fno-stack-protector \
    -Wall -Wextra -Werror -c nwoas_client.c -o "$tmp/nwoas_client.obj"
"$LLVM/lld-link" /entry:mainCRTStartup /subsystem:console /machine:arm64 \
    /nodefaultlib /timestamp:0 /out:NWOAS-S168.EXE "$tmp/nwoas_client.obj" "$tmp/kernel32.lib"
echo "built NWOAS-S168.EXE"
