#include "keyboard.h"

#include <cstddef>
#include <vector>

#include "log.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

// Decode one UTF-8 code point. Surrogate code points are accepted temporarily
// so typeText() can also combine the modified UTF-8 surrogate pairs produced by
// Android's JNI GetStringUTFChars(). They are never injected on their own.
bool decodeUtf8(const std::string& text, size_t offset, uint32_t& codePoint,
                size_t& next) {
    if (offset >= text.size()) return false;

    const auto* bytes = reinterpret_cast<const unsigned char*>(text.data());
    const unsigned char first = bytes[offset];
    if (first <= 0x7F) {
        codePoint = first;
        next = offset + 1;
        return true;
    }

    size_t length = 0;
    uint32_t value = 0;
    uint32_t minimum = 0;
    if (first >= 0xC2 && first <= 0xDF) {
        length = 2;
        value = first & 0x1Fu;
        minimum = 0x80;
    } else if (first >= 0xE0 && first <= 0xEF) {
        length = 3;
        value = first & 0x0Fu;
        minimum = 0x800;
    } else if (first >= 0xF0 && first <= 0xF4) {
        length = 4;
        value = first & 0x07u;
        minimum = 0x10000;
    } else {
        return false;
    }

    if (offset + length > text.size()) return false;
    for (size_t i = 1; i < length; ++i) {
        const unsigned char byte = bytes[offset + i];
        if ((byte & 0xC0u) != 0x80u) return false;
        value = (value << 6) | (byte & 0x3Fu);
    }
    if (value < minimum || value > 0x10FFFFu) return false;

    codePoint = value;
    next = offset + length;
    return true;
}

#ifdef _WIN32
WORD virtualKey(mctl::KeyCode key) {
    switch (key) {
        case mctl::KeyCode::Backspace: return VK_BACK;
        case mctl::KeyCode::Enter: return VK_RETURN;
        case mctl::KeyCode::Tab: return VK_TAB;
        case mctl::KeyCode::Escape: return VK_ESCAPE;
        case mctl::KeyCode::Up: return VK_UP;
        case mctl::KeyCode::Down: return VK_DOWN;
        case mctl::KeyCode::Left: return VK_LEFT;
        case mctl::KeyCode::Right: return VK_RIGHT;
        case mctl::KeyCode::Space: return VK_SPACE;
        case mctl::KeyCode::Delete: return VK_DELETE;
    }
    return 0;
}
#endif

const char* keyName(mctl::KeyCode key) {
    switch (key) {
        case mctl::KeyCode::Backspace: return "Backspace";
        case mctl::KeyCode::Enter: return "Enter";
        case mctl::KeyCode::Tab: return "Tab";
        case mctl::KeyCode::Escape: return "Escape";
        case mctl::KeyCode::Up: return "Up";
        case mctl::KeyCode::Down: return "Down";
        case mctl::KeyCode::Left: return "Left";
        case mctl::KeyCode::Right: return "Right";
        case mctl::KeyCode::Space:return "Space";
        case mctl::KeyCode::Delete:return "Delete";
    }
    return "unknown";
}

}  // namespace

KeyboardInjector::KeyboardInjector(bool dryRun) : dryRun_(dryRun) {}

void KeyboardInjector::typeText(const std::string& utf8) {
    if (utf8.empty()) return;

    std::vector<uint16_t> units;
    units.reserve(utf8.size());
    size_t offset = 0;
    bool valid = true;

    while (offset < utf8.size()) {
        uint32_t codePoint = 0;
        size_t next = offset;
        if (!decodeUtf8(utf8, offset, codePoint, next)) {
            valid = false;
            break;
        }

        if (codePoint >= 0xD800u && codePoint <= 0xDBFFu) {
            uint32_t low = 0;
            size_t afterLow = next;
            if (!decodeUtf8(utf8, next, low, afterLow) ||
                low < 0xDC00u || low > 0xDFFFu) {
                valid = false;
                break;
            }
            codePoint = 0x10000u + ((codePoint - 0xD800u) << 10) +
                        (low - 0xDC00u);
            next = afterLow;
        } else if (codePoint >= 0xDC00u && codePoint <= 0xDFFFu) {
            valid = false;
            break;
        }

        if (codePoint != 0) {
            if (codePoint <= 0xFFFFu) {
                units.push_back(static_cast<uint16_t>(codePoint));
            } else {
                const uint32_t supplementary = codePoint - 0x10000u;
                units.push_back(
                    static_cast<uint16_t>(0xD800u + (supplementary >> 10)));
                units.push_back(static_cast<uint16_t>(
                    0xDC00u + (supplementary & 0x3FFu)));
            }
        }
        offset = next;
    }

    // Never log the text itself, including when verbose or dry-run is enabled.
    if (!valid) {
        logx::warn("Ignored malformed UTF-8 keyboard text");
        return;
    }
    if (units.empty()) return;

    if (dryRun_) {
        logx::event("dry-run: keyboard text (%llu UTF-16 units)",
                    static_cast<unsigned long long>(units.size()));
        return;
    }

#ifdef _WIN32
    std::vector<INPUT> inputs(units.size() * 2);
    for (size_t i = 0; i < units.size(); ++i) {
        inputs[i * 2].type = INPUT_KEYBOARD;
        inputs[i * 2].ki.wScan = units[i];
        inputs[i * 2].ki.dwFlags = KEYEVENTF_UNICODE;
        inputs[i * 2 + 1].type = INPUT_KEYBOARD;
        inputs[i * 2 + 1].ki.wScan = units[i];
        inputs[i * 2 + 1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
    }

    const UINT count = static_cast<UINT>(inputs.size());
    if (SendInput(count, inputs.data(), sizeof(INPUT)) != count) {
        logx::warn("SendInput failed for keyboard text (%lu)",
                   static_cast<unsigned long>(GetLastError()));
    }
#endif
}

void KeyboardInjector::pressKey(mctl::KeyCode key) {
    if (dryRun_) {
        logx::event("dry-run: %s key", keyName(key));
        return;
    }

#ifdef _WIN32
    const WORD vk = virtualKey(key);
    if (vk == 0) return;

    const bool extended = key == mctl::KeyCode::Up ||
                          key == mctl::KeyCode::Down ||
                          key == mctl::KeyCode::Left ||
                          key == mctl::KeyCode::Right ||
                          key == mctl::KeyCode::Delete;
    const DWORD flags = extended ? KEYEVENTF_EXTENDEDKEY : 0;
    const WORD scan =
        static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));

    INPUT inputs[2]{};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = vk;
    inputs[0].ki.wScan = scan;
    inputs[0].ki.dwFlags = flags;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = vk;
    inputs[1].ki.wScan = scan;
    inputs[1].ki.dwFlags = flags | KEYEVENTF_KEYUP;
    if (SendInput(2, inputs, sizeof(INPUT)) != 2) {
        logx::warn("SendInput failed for %s key (%lu)", keyName(key),
                   static_cast<unsigned long>(GetLastError()));
    }
#else
    (void)key;
#endif
}
