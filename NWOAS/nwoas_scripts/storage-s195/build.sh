#!/bin/sh
# Local ARM64 Windows build only. Never launches the result; nothing here touches the Mini.
set -eu
cd "$(dirname "$0")"
LLVM=${NWOAS_LLVM:-/opt/homebrew/opt/llvm/bin}
tmp=$(mktemp -d /tmp/nwoas-s195-storage.XXXXXX)
trap 'rm -rf "$tmp"' EXIT
"$LLVM/llvm-dlltool" -m arm64 -d kernel32.def -l "$tmp/kernel32.lib"
"$LLVM/clang" --target=aarch64-pc-windows-msvc -std=c11 -O2 -ffreestanding -fno-stack-protector -Wall -Wextra -Werror -c iotest.c -o "$tmp/iotest.obj"
"$LLVM/lld-link" /entry:mainCRTStartup /subsystem:console /machine:arm64 /nodefaultlib /timestamp:0 /out:IOTEST.EXE "$tmp/iotest.obj" "$tmp/kernel32.lib"
