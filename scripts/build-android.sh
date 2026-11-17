#!/usr/bin/env bash
# Builds the Android APK with the Gradle wrapper.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VARIANT="${1:-Debug}"
TASK="assemble${VARIANT}"
LOWER="$(echo "$VARIANT" | tr '[:upper:]' '[:lower:]')"

if [ ! -f "$ROOT/third_party/SDL2/CMakeLists.txt" ]; then
    echo "error: SDL2 is missing. Run scripts/fetch-sdl2.sh first." >&2
    exit 1
fi

cd "$ROOT/android"
./gradlew "$TASK"

echo
echo "APK: $ROOT/android/app/build/outputs/apk/${LOWER}/app-${LOWER}.apk"
echo "Install with: adb install -r <apk>"
