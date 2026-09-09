#!/bin/sh
set -eu
cd "$(dirname "$0")"
LLVM=${NWOAS_LLVM:-/opt/homebrew/opt/llvm/bin}
tmp=$(mktemp -d /tmp/nwoas-nwtrim-build.XXXXXX)
trap 'rm -f "$tmp/kernel32.lib" "$tmp/bcrypt.lib" "$tmp/nwtrim.obj"; rmdir "$tmp"' EXIT
"$LLVM/llvm-dlltool" -m arm64 -d kernel32.def -l "$tmp/kernel32.lib"
"$LLVM/llvm-dlltool" -m arm64 -d bcrypt.def -l "$tmp/bcrypt.lib"
"$LLVM/clang" --target=aarch64-pc-windows-msvc -O2 -ffreestanding -fno-stack-protector -Wall -Wextra -Werror -c nwtrim.c -o "$tmp/nwtrim.obj"
"$LLVM/lld-link" /entry:mainCRTStartup /subsystem:console /machine:arm64 /nodefaultlib /timestamp:0 /out:NWTRIM.EXE "$tmp/nwtrim.obj" "$tmp/kernel32.lib" "$tmp/bcrypt.lib"
"$LLVM/llvm-readobj" --file-headers --coff-imports NWTRIM.EXE
