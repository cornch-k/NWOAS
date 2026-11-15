#!/bin/sh
# Generate golden vectors from ../s243_channel.py, then build and run the host
# unit test of the pure wire.h/sha256.h helpers. No disk, device or guest access.
set -eu
cd "$(dirname "$0")"
LLVM=${NWOAS_LLVM:-/opt/homebrew/opt/llvm/bin}
python3 -I gen_vectors.py
bin=$(mktemp "${TMPDIR:-/tmp}/nwoas-s243-test.XXXXXX")
trap 'rm -rf "$bin" "$bin.dSYM" vectors.h' EXIT
"$LLVM/clang" -O1 -g -Wall -Wextra -Werror -std=c11 -fsanitize=address,undefined \
    -fno-sanitize-recover=all test_wire.c -o "$bin"
"$bin"
