#!/usr/bin/env bash
# Configures and builds the server/tools on a POSIX toolchain (e.g. MinGW-w64).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"

cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" --parallel

echo
echo "Built into $BUILD"
