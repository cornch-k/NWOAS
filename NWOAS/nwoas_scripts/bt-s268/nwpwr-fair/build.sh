#!/bin/sh
# S268: build NWPWRF (= power-s260/nwpwr.c with round-robin reaping in io_test)F.EXE (ARM64 Windows, no CRT) the same way as cpufreq-s164/build.sh.
set -eu
cd "$(dirname "$0")"
LLVM=${NWOAS_LLVM:-/opt/homebrew/opt/llvm/bin}
tmp=$(mktemp -d "${TMPDIR:-/tmp}/nwoas-s268-pwrf.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
"$LLVM/llvm-dlltool" -m arm64 -d kernel32.def -l "$tmp/kernel32.lib"
"$LLVM/llvm-dlltool" -m arm64 -d winmm.def -l "$tmp/winmm.lib"
"$LLVM/llvm-dlltool" -m arm64 -d ntdll.def -l "$tmp/ntdll.lib"
"$LLVM/clang" --target=aarch64-pc-windows-msvc -O2 -ffreestanding -fno-stack-protector -fno-builtin -Wall -Wextra -Werror -c nwpwr.c -o "$tmp/nwpwr.obj"
"${NWOAS_LLD:-lld-link}" /entry:mainCRTStartup /subsystem:console /machine:arm64 /nodefaultlib /timestamp:0 /out:NWPWRF.EXE "$tmp/nwpwr.obj" "$tmp/kernel32.lib" "$tmp/winmm.lib" "$tmp/ntdll.lib"
shasum -a 256 NWPWRF.EXE
