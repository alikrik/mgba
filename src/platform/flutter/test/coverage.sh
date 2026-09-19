#!/bin/sh
# Builds the bridge host tests with clang source coverage, runs them, and fails if
# bridge.c line coverage is below MIN (default 85).
#
#   src/platform/flutter/test/coverage.sh [build-dir]
#
# Needs clang plus llvm-profdata/llvm-cov (macOS: via xcrun; Linux: on PATH).
set -eu
HERE=$(cd "$(dirname "$0")/.." && pwd)
BUILD=${1:-build-flutter-coverage}
MIN=${MIN:-85}
if command -v xcrun >/dev/null 2>&1; then LLVM="xcrun"; else LLVM=""; fi

# Both compilers must be clang: mGBA's project also enables C++, and the coverage linker flag
# below would make CMake's check of a GCC C++ compiler fail.
cmake -S "$HERE" -B "$BUILD" -DMGF_BUILD_TESTS=ON -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
	-DCMAKE_C_FLAGS="-fprofile-instr-generate -fcoverage-mapping" \
	-DCMAKE_EXE_LINKER_FLAGS="-fprofile-instr-generate" >/dev/null
cmake --build "$BUILD" --target mgba_flutter_test
LLVM_PROFILE_FILE="$BUILD/bridge.profraw" "$BUILD/mgba_flutter_test"
$LLVM llvm-profdata merge -sparse "$BUILD/bridge.profraw" -o "$BUILD/bridge.profdata"
$LLVM llvm-cov report "$BUILD/mgba_flutter_test" -instr-profile="$BUILD/bridge.profdata" "$HERE/bridge.c"

LINES=$($LLVM llvm-cov export "$BUILD/mgba_flutter_test" -instr-profile="$BUILD/bridge.profdata" \
	-summary-only "$HERE/bridge.c" | sed -n 's/.*"lines":{"count":[0-9]*,"covered":[0-9]*,"percent":\([0-9.]*\).*/\1/p' | head -n 1)
echo "bridge.c line coverage: ${LINES}% (minimum ${MIN}%)"
awk -v got="$LINES" -v min="$MIN" 'BEGIN { exit (got + 0 >= min + 0) ? 0 : 1 }' || {
	echo "Coverage below threshold" >&2
	exit 1
}
