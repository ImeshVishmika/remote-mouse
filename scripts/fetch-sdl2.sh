#!/usr/bin/env bash
# Fetches SDL2 (pinned release) into third_party/SDL2.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="$ROOT/third_party/SDL2"
VERSION="${1:-release-2.30.9}"

patch_android_backspace() {
    # Backport upstream SDL PR #15155: API 30+ also needs this workaround when
    # text input is restarted, or Backspace can stop producing SDL key events.
    ACTIVITY="$DEST/android-project/app/src/main/java/org/libsdl/app/SDLActivity.java" \
        python3 - <<'PY'
import os
import pathlib
import re

path = pathlib.Path(os.environ["ACTIVITY"])
if not path.is_file():
    raise SystemExit(f"error: SDLActivity.java was not found at {path}")
text = path.read_text(encoding="utf-8")
pattern = re.compile(
    r"^        if \(Build\.VERSION\.SDK_INT <= 29 /\* Android 10\.0 \(Q\) \*/\) \{\n"
    r"(?P<body>.*?)^        \}\n",
    re.MULTILINE | re.DOTALL,
)
match = pattern.search(text)
if match:
    path.write_text(text[:match.start()] + match.group("body") + text[match.end():],
                    encoding="utf-8")
    print("Applied the SDL Android API 30+ Backspace fix.")
elif "public boolean deleteSurroundingText" in text:
    print("SDL Android Backspace fix is already present.")
else:
    raise SystemExit("error: could not locate SDL Android deleteSurroundingText")
PY
}

if [ -f "$DEST/CMakeLists.txt" ]; then
    patch_android_backspace
    echo "SDL2 is already present at $DEST"
    exit 0
fi

mkdir -p "$(dirname "$DEST")"

if command -v git >/dev/null 2>&1; then
    git clone --depth 1 --branch "$VERSION" \
        https://github.com/libsdl-org/SDL.git "$DEST"
    patch_android_backspace
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
patch_android_backspace
echo "SDL2 $VERSION extracted into $DEST"
