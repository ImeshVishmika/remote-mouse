#pragma once

#include <cstdint>
#include <string>

struct ServerConfig {
    uint16_t port = 47824;
    uint16_t discoveryPort = 47824;
    uint32_t pin = 0;  // 0 means "generate a random PIN on startup"
    std::string name;
    float sensitivity = 1.0f;
    bool dryRun = false;
    bool discovery = true;
    bool verbose = false;
};

enum class ConfigResult {
    Ok,
    Help,
    Version,
    Error,
};

ConfigResult parseArgs(int argc, char** argv, ServerConfig& config,
                       std::string& error);

void printUsage(const char* executable);

std::string defaultServerName();
uint32_t randomPin();
std::string formatPin(uint32_t pin);
