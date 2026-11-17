#pragma once

#include <cstdarg>

namespace logx {

enum class Level {
    Info,
    Warn,
    Error,
    Event,
};

// Thread-safe printf-style logging with timestamps and optional colours.
void info(const char* format, ...);
void warn(const char* format, ...);
void error(const char* format, ...);
void event(const char* format, ...);

void writeV(Level level, const char* format, std::va_list args);

// Colour support is detected once on first use.
bool colorEnabled();

}  // namespace logx
