#!/bin/sh
# Generate golden vectors from the real ../channel.py, then build and run the
# host-native unit test for the pure wire.h helpers. This exercises ONLY
# in-memory validation logic; it does not touch any disk, device, or guest.
set -eu
cd "$(dirname "$0")"
LLVM=${NWOAS_LLVM:-/opt/homebrew/opt/llvm/bin}
python3 gen_vectors.py
"$LLVM/clang" -O2 -Wall -Wextra -Werror -std=c11 test_wire.c -o /tmp/nwoas-s168-test
/tmp/nwoas-s168-test
