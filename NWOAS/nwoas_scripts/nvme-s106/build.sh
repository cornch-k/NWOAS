#!/bin/sh
set -eu
cd "$(dirname "$0")"
LLVM=${NWOAS_LLVM:-/opt/homebrew/opt/llvm/bin}
tmp=$(mktemp -d /tmp/nwoas-nwhash-build.XXXXXX)
trap 'rm -f "$tmp/kernel32.lib" "$tmp/bcrypt.lib" "$tmp/nwhash.obj"; rmdir "$tmp"' EXIT
"$LLVM/llvm-dlltool" -m arm64 -d kernel32.def -l "$tmp/kernel32.lib"
"$LLVM/llvm-dlltool" -m arm64 -d bcrypt.def -l "$tmp/bcrypt.lib"
"$LLVM/clang" --target=aarch64-pc-windows-msvc -O2 -ffreestanding -fno-stack-protector -Wall -Wextra -Werror -c nwhash.c -o "$tmp/nwhash.obj"
"$LLVM/lld-link" /entry:mainCRTStartup /subsystem:console /machine:arm64 /nodefaultlib /timestamp:0 /out:NWHASH.EXE "$tmp/nwhash.obj" "$tmp/kernel32.lib" "$tmp/bcrypt.lib"
"$LLVM/llvm-readobj" --file-headers --coff-imports NWHASH.EXE
