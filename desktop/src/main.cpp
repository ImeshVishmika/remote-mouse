#include <cstdio>
#include <string>

#include "config.h"
#include "log.h"
#include "platform.h"
#include "protocol.h"
#include "server.h"

int main(int argc, char** argv) {
    platformInit();
    if (!platformInstallQuitHandler()) {
        // Not fatal: the console 'q' key still works.
        logx::warn("Could not install the Ctrl+C handler; use q to quit.");
    }

    const char* executable = (argc > 0 && argv[0] != nullptr)
                                 ? argv[0]
                                 : "remote-mouse-server";

    ServerConfig config;
    std::string error;
    const ConfigResult result = parseArgs(argc, argv, config, error);

    if (result == ConfigResult::Help) {
        printUsage(executable);
        return 0;
    }
    if (result == ConfigResult::Version) {
        std::printf("Remote Mouse protocol v%u\n",
                    static_cast<unsigned>(mctl::kProtocolVersion));
        return 0;
    }
    if (result == ConfigResult::Error) {
        std::fprintf(stderr, "error: %s\n\n", error.c_str());
        printUsage(executable);
        return 2;
    }

    if (config.pin == 0) config.pin = randomPin();
    if (config.name.empty()) config.name = defaultServerName();

    Server server(config);
    return server.run();
}
