#include "log.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace logx {
namespace {

const char* levelTag(Level level) {
    switch (level) {
        case Level::Info: return "INFO";
        case Level::Warn: return "WARN";
        case Level::Error: return "ERR ";
        case Level::Event: return "EVT ";
    }
    return "INFO";
}

const char* levelColor(Level level) {
    switch (level) {
        case Level::Info: return "";
        case Level::Warn: return "\x1b[33m";
        case Level::Error: return "\x1b[31m";
        case Level::Event: return "\x1b[36m";
    }
    return "";
}

std::mutex& outputMutex() {
    static std::mutex mutex;
    return mutex;
}

std::string timestamp() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    const std::time_t seconds = system_clock::to_time_t(now);

    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif

    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d.%03d", local.tm_hour,
                  local.tm_min, local.tm_sec, static_cast<int>(ms.count()));
    return buffer;
}

}  // namespace

bool colorEnabled() {
    static const bool enabled = [] {
#ifdef _WIN32
        const HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
        if (handle == INVALID_HANDLE_VALUE || handle == nullptr) return false;
        DWORD mode = 0;
        if (!GetConsoleMode(handle, &mode)) return false;
        if ((mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0) return true;
        return SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
        return isatty(fileno(stdout)) != 0;
#endif
    }();
    return enabled;
}

void writeV(Level level, const char* format, std::va_list args) {
    char message[1024];
    std::vsnprintf(message, sizeof(message), format, args);
    message[sizeof(message) - 1] = '\0';

    const bool useColor = colorEnabled();
    const std::string line = timestamp();

    std::lock_guard<std::mutex> lock(outputMutex());
    if (useColor) {
        std::fprintf(stdout, "\x1b[90m%s\x1b[0m %s%s\x1b[0m %s\n", line.c_str(),
                     levelColor(level), levelTag(level), message);
    } else {
        std::fprintf(stdout, "%s %s %s\n", line.c_str(), levelTag(level), message);
    }
    std::fflush(stdout);
}

void info(const char* format, ...) {
    std::va_list args;
    va_start(args, format);
    writeV(Level::Info, format, args);
    va_end(args);
}

void warn(const char* format, ...) {
    std::va_list args;
    va_start(args, format);
    writeV(Level::Warn, format, args);
    va_end(args);
}

void error(const char* format, ...) {
    std::va_list args;
    va_start(args, format);
    writeV(Level::Error, format, args);
    va_end(args);
}

void event(const char* format, ...) {
    std::va_list args;
    va_start(args, format);
    writeV(Level::Event, format, args);
    va_end(args);
}

}  // namespace logx
