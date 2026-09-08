#!/bin/sh
set -eu
cd "$(dirname "$0")"
LLVM=${NWOAS_LLVM:-/opt/homebrew/opt/llvm/bin}
tmp=$(mktemp -d /tmp/nwoas-s118-build.XXXXXX)
trap 'rm -f "$tmp/kernel32.lib" "$tmp/nwesp.obj"; rmdir "$tmp"' EXIT
"$LLVM/llvm-dlltool" -m arm64 -d kernel32.def -l "$tmp/kernel32.lib"
"$LLVM/clang" --target=aarch64-pc-windows-msvc -O2 -ffreestanding -fno-stack-protector -Wall -Wextra -Werror -c nwesp.c -o "$tmp/nwesp.obj"
"$LLVM/lld-link" /entry:mainCRTStartup /subsystem:console /machine:arm64 /nodefaultlib /timestamp:0 /out:NWESP.EXE "$tmp/nwesp.obj" "$tmp/kernel32.lib"
"$LLVM/llvm-readobj" --file-headers --coff-imports NWESP.EXE
