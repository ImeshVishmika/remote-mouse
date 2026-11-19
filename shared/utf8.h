#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace mctl {
namespace detail {

// Android's JNI GetStringUTFChars() encodes UTF-16 surrogates individually as
// three-byte modified UTF-8 sequences instead of using standard four-byte UTF-8.
inline bool decodeAndroidSurrogate(const std::string& text, size_t offset,
                                   uint16_t& surrogate, size_t& next) {
    if (offset + 3 > text.size()) return false;
    const auto* bytes = reinterpret_cast<const unsigned char*>(text.data());
    const unsigned char first = bytes[offset];
    const unsigned char second = bytes[offset + 1];
    const unsigned char third = bytes[offset + 2];
    if (first != 0xEDu || second < 0xA0u || second > 0xBFu ||
        (third & 0xC0u) != 0x80u) {
        return false;
    }

    surrogate = static_cast<uint16_t>(0xD000u |
                                      ((second & 0x3Fu) << 6) |
                                      (third & 0x3Fu));
    next = offset + 3;
    return true;
}

inline void appendSupplementaryUtf8(uint32_t codePoint, std::string& output) {
    output.push_back(static_cast<char>(0xF0u | (codePoint >> 18)));
    output.push_back(static_cast<char>(0x80u | ((codePoint >> 12) & 0x3Fu)));
    output.push_back(static_cast<char>(0x80u | ((codePoint >> 6) & 0x3Fu)));
    output.push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
}

}  // namespace detail

// Convert Android/JNI modified UTF-8 surrogate pairs to standard UTF-8. SDL's
// fixed-size text event can split a pair across calls, so the caller retains
// pendingHighSurrogate until the next event. Other bytes pass through unchanged.
inline std::string normalizeAndroidText(const std::string& text,
                                        uint16_t& pendingHighSurrogate) {
    std::string output;
    output.reserve(text.size());
    size_t offset = 0;

    if (pendingHighSurrogate != 0 && !text.empty()) {
        uint16_t low = 0;
        size_t next = 0;
        if (detail::decodeAndroidSurrogate(text, 0, low, next) &&
            low >= 0xDC00u && low <= 0xDFFFu) {
            const uint32_t codePoint =
                0x10000u +
                ((static_cast<uint32_t>(pendingHighSurrogate) - 0xD800u) << 10) +
                (static_cast<uint32_t>(low) - 0xDC00u);
            detail::appendSupplementaryUtf8(codePoint, output);
            offset = next;
        }
        pendingHighSurrogate = 0;
    }

    while (offset < text.size()) {
        uint16_t surrogate = 0;
        size_t next = offset;
        if (!detail::decodeAndroidSurrogate(text, offset, surrogate, next)) {
            output.push_back(text[offset++]);
            continue;
        }

        if (surrogate >= 0xD800u && surrogate <= 0xDBFFu) {
            uint16_t low = 0;
            size_t afterLow = next;
            if (detail::decodeAndroidSurrogate(text, next, low, afterLow) &&
                low >= 0xDC00u && low <= 0xDFFFu) {
                const uint32_t codePoint =
                    0x10000u +
                    ((static_cast<uint32_t>(surrogate) - 0xD800u) << 10) +
                    (static_cast<uint32_t>(low) - 0xDC00u);
                detail::appendSupplementaryUtf8(codePoint, output);
                offset = afterLow;
            } else {
                if (next == text.size()) pendingHighSurrogate = surrogate;
                // An unmatched surrogate is not valid Unicode text; omit it.
                offset = next;
            }
        } else {
            // A low surrogate without a preceding high surrogate is invalid.
            offset = next;
        }
    }

    return output;
}

}  // namespace mctl
