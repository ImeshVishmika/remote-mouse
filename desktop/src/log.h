#pragma once

#include <cstdarg>
#include <deque>
#include <string>
#include <vector>

// Thread-safe printf-style logging with timestamps and optional colours.
// Recent lines are kept in an in-memory ring so a GUI (or other consumer) can
// drain them on a timer without losing output written from worker threads.
namespace logx {

enum class Level {
    Info,
    Warn,
    Error,
    Event,
};

void info(const char* format, ...);
void warn(const char* format, ...);
void error(const char* format, ...);
void event(const char* format, ...);

void writeV(Level level, const char* format, std::va_list args);

// Push an already-formatted line (called internally by writeV); kept in a ring
// buffer so the GUI can poll it.
void pushLog(std::string line);

// Drain and clear every log line currently buffered. Callers get a snapshot;
// the returned vector is safe to iterate from any thread.
std::vector<std::string> drainLogs();

// Colour support is detected once on first use.
bool colorEnabled();

}  // namespace logx
