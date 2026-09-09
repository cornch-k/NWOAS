#!/bin/sh
# Build and run the host tests for ../Include/Library/NwoasSplitMemoryMap.h
set -eu
cd "$(dirname "$0")"
mkdir -p build
clang -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-unused-function \
  -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I stub -I ../Include \
  test_split_memory_map.c stub/BaseMemoryLibStub.c \
  -o build/test_split_memory_map
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 ./build/test_split_memory_map
