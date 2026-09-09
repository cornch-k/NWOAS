#!/bin/sh
set -eu
cd "$(dirname "$0")"
LLVM=/opt/homebrew/opt/llvm/bin
tmp=$(mktemp -d /tmp/nwoas-s176.XXXXXX)
trap 'rm -rf "$tmp"' EXIT
for lib in kernel32 advapi32 userenv wtsapi32; do "$LLVM/llvm-dlltool" -m arm64 -d "$lib.def" -l "$tmp/$lib.lib"; done
"$LLVM/clang" --target=aarch64-pc-windows-msvc -O2 -ffreestanding -fno-stack-protector -Wall -Wextra -Werror -c read_session.c -o "$tmp/session.obj"
"$LLVM/lld-link" /entry:mainCRTStartup /subsystem:console /machine:arm64 /nodefaultlib /timestamp:0 /out:USERREAD.EXE "$tmp/session.obj" "$tmp/kernel32.lib" "$tmp/advapi32.lib" "$tmp/userenv.lib" "$tmp/wtsapi32.lib"
