#!/usr/bin/env bash
# Build and run the native unit-test suite (no external dependencies).
# Every module's tests are compiled together into one runner.
set -euo pipefail
cd "$(dirname "$0")"

CXX=${CXX:-g++}
FLAGS="-std=c++17 -O2 -Wall -Wextra -Iinclude -Itests"

SRCS=$(find src -name '*.cpp')
TESTS=$(find tests -name '*.cpp')

mkdir -p build
echo "==> Compiling tests ..."
$CXX $FLAGS $SRCS $TESTS -o build/run_tests
echo "==> Running ..."
./build/run_tests
