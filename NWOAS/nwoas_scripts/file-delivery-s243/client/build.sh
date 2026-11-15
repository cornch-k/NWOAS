#!/bin/sh
# Build the S243 no-CRT ARM64 Windows delivery client, same recipe as
# file-delivery-s168/client/build.sh: freestanding, no default libs, own
# minimal kernel32 import library, zero warnings (-Wall -Wextra -Werror).
set -eu
cd "$(dirname "$0")"
LLVM=${NWOAS_LLVM:-/opt/homebrew/opt/llvm/bin}
LLD=${NWOAS_LLD_LINK:-$(command -v lld-link || echo "$LLVM/lld-link")}
tmp=$(mktemp -d "${TMPDIR:-/tmp}/nwoas-s243-build.XXXXXX")
trap 'rm -f "$tmp/kernel32.lib" "$tmp/nwget243.obj"; rmdir "$tmp"' EXIT
"$LLVM/llvm-dlltool" -m arm64 -d kernel32.def -l "$tmp/kernel32.lib"
"$LLVM/clang" --target=aarch64-pc-windows-msvc -O2 -ffreestanding -fno-stack-protector \
    -Wall -Wextra -Werror -c nwget243.c -o "$tmp/nwget243.obj"
"$LLD" /entry:mainCRTStartup /subsystem:console /machine:arm64 \
    /nodefaultlib /timestamp:0 /out:NWGET243.EXE "$tmp/nwget243.obj" "$tmp/kernel32.lib"
echo "built NWGET243.EXE"
