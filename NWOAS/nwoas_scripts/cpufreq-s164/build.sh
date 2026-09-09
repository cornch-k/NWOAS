#!/bin/sh
set -eu
cd "$(dirname "$0")"
LLVM=${NWOAS_LLVM:-/opt/homebrew/opt/llvm/bin}
tmp=$(mktemp -d /tmp/nwoas-s164-cpu.XXXXXX)
trap 'rm -rf "$tmp"' EXIT
"$LLVM/llvm-dlltool" -m arm64 -d kernel32.def -l "$tmp/kernel32.lib"
"$LLVM/clang" --target=aarch64-pc-windows-msvc -O2 -ffreestanding -fno-stack-protector -Wall -Wextra -Werror -c percpu.c -o "$tmp/percpu.obj"
"$LLVM/lld-link" /entry:mainCRTStartup /subsystem:console /machine:arm64 /nodefaultlib /timestamp:0 /out:PERCPU.EXE "$tmp/percpu.obj" "$tmp/kernel32.lib"
