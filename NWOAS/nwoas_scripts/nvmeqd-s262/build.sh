#!/bin/sh
# S262: build NWQD.EXE (ARM64 Windows, no CRT) the same way as power-s260/build.sh.
set -eu
cd "$(dirname "$0")"
LLVM=${NWOAS_LLVM:-/opt/homebrew/opt/llvm/bin}
tmp=$(mktemp -d "${TMPDIR:-/tmp}/nwoas-s262-qd.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
"$LLVM/llvm-dlltool" -m arm64 -d kernel32.def -l "$tmp/kernel32.lib"
"$LLVM/clang" --target=aarch64-pc-windows-msvc -O2 -ffreestanding -fno-stack-protector -fno-builtin -Wall -Wextra -Werror -c nwqd.c -o "$tmp/nwqd.obj"
"${NWOAS_LLD:-lld-link}" /entry:mainCRTStartup /subsystem:console /machine:arm64 /nodefaultlib /timestamp:0 /out:NWQD.EXE "$tmp/nwqd.obj" "$tmp/kernel32.lib"
shasum -a 256 NWQD.EXE
