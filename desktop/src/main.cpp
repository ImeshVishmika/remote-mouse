#include <cstdio>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "config.h"
#include "gui.h"
#include "log.h"
#include "platform.h"
#include "protocol.h"
#include "server.h"

int main(int argc, char** argv) {
    platformInit();
    if (!platformInstallQuitHandler()) {
        // Not fatal: the window close button still works.
        logx::warn("Could not install the Ctrl+C handler; use the window to "
                   "quit.");
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

#ifdef _WIN32
    if (config.gui) {
        ServerGui gui(config, GetModuleHandleA(nullptr));
        return gui.run(SW_SHOWDEFAULT);
    }
#else
    (void)config;  // GUI is Windows-only on this host.
#endif

    Server server(config);
    return server.run();
}
