#include "config.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>

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

constexpr float kMinSensitivity = 0.1f;
constexpr float kMaxSensitivity = 10.0f;

bool parseUnsigned(const std::string& text, unsigned long maximum,
                   unsigned long& value) {
    if (text.empty()) return false;
    errno = 0;
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(text.c_str(), &end, 10);
    if (errno != 0 || end == text.c_str() || *end != '\0') return false;
    if (parsed > maximum) return false;
    value = parsed;
    return true;
}

bool parseFloat(const std::string& text, float minimum, float maximum,
                float& value) {
    if (text.empty()) return false;
    errno = 0;
    char* end = nullptr;
    const float parsed = std::strtof(text.c_str(), &end);
    if (errno != 0 || end == text.c_str() || *end != '\0') return false;
    if (!(parsed >= minimum && parsed <= maximum)) return false;
    value = parsed;
    return true;
}

}  // namespace

ConfigResult parseArgs(int argc, char** argv, ServerConfig& config,
                       std::string& error) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto requireValue = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                error = std::string("missing value for ") + name;
                return std::string();
            }
            return argv[++i];
        };

        if (arg == "--help" || arg == "-h") {
            return ConfigResult::Help;
        }
        if (arg == "--version") {
            return ConfigResult::Version;
        }
        if (arg == "--port" || arg == "--discovery-port") {
            const std::string value = requireValue(arg.c_str());
            if (!error.empty()) return ConfigResult::Error;
            unsigned long parsed = 0;
            if (!parseUnsigned(value, 65535, parsed) || parsed == 0) {
                error = "invalid port: " + value;
                return ConfigResult::Error;
            }
            if (arg == "--port") {
                config.port = static_cast<uint16_t>(parsed);
            } else {
                config.discoveryPort = static_cast<uint16_t>(parsed);
            }
        } else if (arg == "--pin") {
            const std::string value = requireValue("--pin");
            if (!error.empty()) return ConfigResult::Error;
            unsigned long parsed = 0;
            if (!parseUnsigned(value, 999999, parsed) || parsed == 0) {
                error = "invalid PIN (expected 1-999999): " + value;
                return ConfigResult::Error;
            }
            config.pin = static_cast<uint32_t>(parsed);
        } else if (arg == "--name") {
            const std::string value = requireValue("--name");
            if (!error.empty()) return ConfigResult::Error;
            if (value.size() > 64) {
                error = "--name is too long (max 64 bytes)";
                return ConfigResult::Error;
            }
            config.name = value;
        } else if (arg == "--sensitivity") {
            const std::string value = requireValue("--sensitivity");
            if (!error.empty()) return ConfigResult::Error;
            if (!parseFloat(value, kMinSensitivity, kMaxSensitivity,
                            config.sensitivity)) {
                error = "invalid sensitivity (expected 0.1-10.0): " + value;
                return ConfigResult::Error;
            }
        } else if (arg == "--dry-run") {
            config.dryRun = true;
        } else if (arg == "--no-discovery") {
            config.discovery = false;
        } else if (arg == "--verbose" || arg == "-v") {
            config.verbose = true;
        } else if (arg == "--gui") {
            config.gui = true;
        } else {
            error = "unknown option: " + arg;
            return ConfigResult::Error;
        }
    }
    return ConfigResult::Ok;
}

void printUsage(const char* executable) {
    std::printf(
        "Remote Mouse desktop server\n"
        "\n"
        "Usage: %s [options]\n"
        "\n"
        "Options:\n"
        "  --port N            TCP port to listen on (default 47824)\n"
        "  --discovery-port N  UDP discovery port (default 47824)\n"
        "  --pin NNNNNN        fixed pairing PIN (default: random each run)\n"
        "  --name TEXT         server name shown on the phone (default: hostname)\n"
        "  --sensitivity F     cursor speed multiplier (0.1-10.0, default 1.0)\n"
        "  --dry-run           log input events without moving the real cursor\n"
        "  --no-discovery      do not answer UDP discovery probes\n"
        "  --gui               run the native desktop UI instead of the console\n"
        "  --verbose, -v       log every input event\n"
        "  --version           print the protocol version and exit\n"
        "  --help, -h          show this help\n"
        "\n"
        "In-app keys: q quit, p show PIN, s status, k kick device, h help\n"
        "\n"
        "The PIN is printed on startup. Pairing traffic is plaintext on the\n"
        "local network, so only run this on a network you trust.\n",
        executable);
}

std::string defaultServerName() {
#ifdef _WIN32
    char buffer[256] = {0};
    DWORD size = static_cast<DWORD>(sizeof(buffer));
    if (GetComputerNameA(buffer, &size) && size > 0) {
        return std::string(buffer, size);
    }
#endif
    return "remote-mouse-pc";
}

uint32_t randomPin() {
    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<uint32_t> distribution(1, 999999);
    return distribution(rng);
}

std::string formatPin(uint32_t pin) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%06u", static_cast<unsigned>(pin));
    return buffer;
}
