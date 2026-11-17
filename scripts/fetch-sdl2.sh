#!/usr/bin/env bash
# Fetches SDL2 (pinned release) into third_party/SDL2.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="$ROOT/third_party/SDL2"
VERSION="${1:-release-2.30.9}"

if [ -f "$DEST/CMakeLists.txt" ]; then
    echo "SDL2 is already present at $DEST"
    exit 0
fi

mkdir -p "$(dirname "$DEST")"

if command -v git >/dev/null 2>&1; then
    git clone --depth 1 --branch "$VERSION" \
        https://github.com/libsdl-org/SDL.git "$DEST"
    echo "SDL2 $VERSION cloned into $DEST"
    exit 0
fi

ARCHIVE_URL="https://codeload.github.com/libsdl-org/SDL/tar.gz/refs/tags/$VERSION"
TEMP="$(mktemp -d)"
trap 'rm -rf "$TEMP"' EXIT

echo "Downloading $ARCHIVE_URL ..."
curl -fsSL "$ARCHIVE_URL" -o "$TEMP/sdl2.tar.gz"
tar -xzf "$TEMP/sdl2.tar.gz" -C "$TEMP"

EXTRACTED="$(find "$TEMP" -maxdepth 1 -type d -name 'SDL-*' | head -n 1)"
if [ -z "$EXTRACTED" ]; then
    echo "error: archive did not contain an SDL-* directory" >&2
    exit 1
fi

mv "$EXTRACTED" "$DEST"
echo "SDL2 $VERSION extracted into $DEST"
