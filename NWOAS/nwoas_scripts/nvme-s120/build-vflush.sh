#!/bin/sh
set -eu
cd "$(dirname "$0")"
LLVM=${NWOAS_LLVM:-/opt/homebrew/opt/llvm/bin}
tmp=$(mktemp -d /tmp/nwoas-s118-build.XXXXXX)
trap 'rm -f "$tmp/kernel32.lib" "$tmp/nwvflush.obj"; rmdir "$tmp"' EXIT
"$LLVM/llvm-dlltool" -m arm64 -d vflush.def -l "$tmp/kernel32.lib"
"$LLVM/clang" --target=aarch64-pc-windows-msvc -O2 -ffreestanding -fno-stack-protector -Wall -Wextra -Werror -c nwvflush.c -o "$tmp/nwvflush.obj"
"$LLVM/lld-link" /entry:mainCRTStartup /subsystem:console /machine:arm64 /nodefaultlib /timestamp:0 /out:NWVFLUSH.EXE "$tmp/nwvflush.obj" "$tmp/kernel32.lib"
"$LLVM/llvm-readobj" --file-headers --coff-imports NWVFLUSH.EXE
