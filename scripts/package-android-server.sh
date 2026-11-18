#!/usr/bin/env bash
# Cross-compiles the Windows server and bundles it as an Android asset.
# The phone serves this exact file from its local HTTP download page.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ZIG="${ZIG:-zig}"
OUT_DIR="$ROOT/android/app/src/main/assets"
OUT="$OUT_DIR/remote_mouse_server.exe"
BUILD_DIR="$ROOT/build-zig/android-server"
BUILD="$BUILD_DIR/remote_mouse_server.exe"

# Zig can also emit a PDB and import library next to -o. Keep those byproducts
# out of Android assets so the APK contains only the downloadable executable.
mkdir -p "$OUT_DIR" "$BUILD_DIR"

"$ZIG" c++ \
    -target x86_64-windows-gnu \
    -std=c++17 \
    -O2 \
    -Wall -Wextra -Wpedantic \
    -I "$ROOT/shared" \
    -I "$ROOT/desktop/src" \
    "$ROOT"/desktop/src/*.cpp \
    -lws2_32 \
    -liphlpapi \
    -luser32 \
    -lcomctl32 \
    -o "$BUILD"

cp "$BUILD" "$OUT"
ls -lh "$OUT"
