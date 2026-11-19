#!/bin/sh
# S262S: build NWIOP.EXE (ARM64 Windows, no CRT) the same way as build.sh (NWQD.EXE is not touched).
set -eu
cd "$(dirname "$0")"
LLVM=${NWOAS_LLVM:-/opt/homebrew/opt/llvm/bin}
tmp=$(mktemp -d "${TMPDIR:-/tmp}/nwoas-s262s-iop.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
"$LLVM/llvm-dlltool" -m arm64 -d nwiop-kernel32.def -l "$tmp/kernel32.lib"
"$LLVM/llvm-dlltool" -m arm64 -d nwiop-ntdll.def -l "$tmp/ntdll.lib"
"$LLVM/clang" --target=aarch64-pc-windows-msvc -O2 -ffreestanding -fno-stack-protector -fno-builtin -Wall -Wextra -Werror -c nwiop.c -o "$tmp/nwiop.obj"
"${NWOAS_LLD:-lld-link}" /entry:mainCRTStartup /subsystem:console /machine:arm64 /nodefaultlib /timestamp:0 /out:NWIOP.EXE "$tmp/nwiop.obj" "$tmp/kernel32.lib" "$tmp/ntdll.lib"
shasum -a 256 NWIOP.EXE
