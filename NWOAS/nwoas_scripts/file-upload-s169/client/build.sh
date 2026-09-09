#!/bin/sh
set -eu
cd "$(dirname "$0")"
LLVM=/opt/homebrew/opt/llvm/bin
tmp=$(mktemp -d /tmp/nwoas-s169-build.XXXXXX)
trap 'rm -f "$tmp/kernel32.lib" "$tmp/nwput.obj"; rmdir "$tmp"' EXIT
"$LLVM/llvm-dlltool" -m arm64 -d kernel32.def -l "$tmp/kernel32.lib"
"$LLVM/clang" --target=aarch64-pc-windows-msvc -O2 -ffreestanding -fno-stack-protector -Wall -Wextra -Werror -c nwput.c -o "$tmp/nwput.obj"
"$LLVM/lld-link" /entry:mainCRTStartup /subsystem:console /machine:arm64 /nodefaultlib /timestamp:0 /out:NWPUT.EXE "$tmp/nwput.obj" "$tmp/kernel32.lib"
