// Command-line client that exercises the server without a phone.
//
//   remote_mouse_server --dry-run --pin 123456
//   mouse_sim --pin 123456
//
// Without --dry-run on the server this really moves the mouse cursor.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include "connection.h"
#include "net_types.h"
#include "protocol.h"

namespace {

struct Options {
    std::string host = "127.0.0.1";
    uint16_t port = mctl::kDefaultPort;
    uint32_t pin = 0;
    std::string name = "mouse-sim";
};

void printUsage(const char* executable) {
    std::printf(
        "Remote Mouse protocol simulator\n"
        "\n"
        "Usage: %s --pin NNNNNN [options]\n"
        "\n"
        "Options:\n"
        "  --pin NNNNNN   pairing PIN printed by the server (required)\n"
        "  --host ADDRESS server IPv4 address (default 127.0.0.1)\n"
        "  --port N       server TCP port (default 47824)\n"
        "  --name TEXT    device name sent during pairing\n"
        "  --help, -h     show this help\n",
        executable);
}

bool parseUnsigned(const char* text, unsigned long maximum,
                   unsigned long& value) {
    if (text == nullptr || *text == '\0') return false;
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(text, &end, 10);
    if (end == text || *end != '\0' || parsed > maximum) return false;
    value = parsed;
    return true;
}

bool expect(Connection& connection, mctl::MsgType expected, mctl::Frame& frame,
            int timeoutMs, const char* description) {
    for (;;) {
        std::string error;
        const ReadResult result = connection.readFrame(frame, timeoutMs, &error);

        if (result == ReadResult::Frame) {
            if (frame.type == expected) return true;
            if (frame.type == mctl::MsgType::Pong) continue;

            if (frame.type == mctl::MsgType::Error) {
                mctl::ErrorInfo info;
                if (mctl::parseError(frame, info)) {
                    std::printf("[sim] server error: %s\n", info.message.c_str());
                }
                return false;
            }

            std::printf("[sim] unexpected message 0x%02X while waiting for %s\n",
                        static_cast<unsigned>(static_cast<uint8_t>(frame.type)),
                        description);
            return false;
        }
        if (result == ReadResult::Timeout) {
            std::printf("[sim] timed out waiting for %s\n", description);
            return false;
        }
        if (result == ReadResult::Closed) {
            std::printf("[sim] server closed the connection\n");
            return false;
        }
        std::printf("[sim] read error: %s\n", error.c_str());
        return false;
    }
}

bool moveBy(Connection& connection, int totalX, int totalY, int steps) {
    int sentX = 0;
    int sentY = 0;
    for (int step = 1; step <= steps; ++step) {
        const int targetX = totalX * step / steps;
        const int targetY = totalY * step / steps;
        const int dx = targetX - sentX;
        const int dy = targetY - sentY;
        sentX = targetX;
        sentY = targetY;

        if (dx != 0 || dy != 0) {
            if (!connection.sendFrame(
                    mctl::makeMove(mctl::clampToI16(dx), mctl::clampToI16(dy)))) {
                return false;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return true;
}

bool click(Connection& connection, uint8_t button) {
    const auto code = static_cast<mctl::MouseButton>(button);
    if (!connection.sendFrame(mctl::makeButton(code, true))) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    return connection.sendFrame(mctl::makeButton(code, false));
}

bool measurePing(Connection& connection, uint32_t token, int& roundTripMs) {
    const auto started = std::chrono::steady_clock::now();
    if (!connection.sendFrame(mctl::makePing(token))) return false;

    mctl::Frame frame;
    for (;;) {
        std::string error;
        const ReadResult result = connection.readFrame(frame, 2000, &error);
        if (result == ReadResult::Timeout) {
            std::printf("[sim] ping timed out\n");
            return false;
        }
        if (result == ReadResult::Closed) {
            std::printf("[sim] server closed the connection during ping\n");
            return false;
        }
        if (result == ReadResult::Error) {
            std::printf("[sim] ping read error: %s\n", error.c_str());
            return false;
        }
        if (frame.type != mctl::MsgType::Pong) continue;

        uint32_t answer = 0;
        if (!mctl::parseToken(frame, answer) || answer != token) continue;
        roundTripMs = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started)
                .count());
        return true;
    }
}

}  // namespace

int main(int argc, char** argv) {
    Options options;

    for (int i = 1; i < argc; ++i) {
        const char* argument = argv[i];
        auto nextValue = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "error: missing value for %s\n", name);
                return nullptr;
            }
            return argv[++i];
        };

        if (std::strcmp(argument, "--help") == 0 ||
            std::strcmp(argument, "-h") == 0) {
            printUsage(argv[0]);
            return 0;
        }
        if (std::strcmp(argument, "--pin") == 0) {
            const char* value = nextValue("--pin");
            if (value == nullptr) return 2;
            unsigned long parsed = 0;
            if (!parseUnsigned(value, 999999, parsed) || parsed == 0) {
                std::fprintf(stderr, "error: invalid PIN: %s\n", value);
                return 2;
            }
            options.pin = static_cast<uint32_t>(parsed);
        } else if (std::strcmp(argument, "--port") == 0) {
            const char* value = nextValue("--port");
            if (value == nullptr) return 2;
            unsigned long parsed = 0;
            if (!parseUnsigned(value, 65535, parsed) || parsed == 0) {
                std::fprintf(stderr, "error: invalid port: %s\n", value);
                return 2;
            }
            options.port = static_cast<uint16_t>(parsed);
        } else if (std::strcmp(argument, "--host") == 0) {
            const char* value = nextValue("--host");
            if (value == nullptr) return 2;
            options.host = value;
        } else if (std::strcmp(argument, "--name") == 0) {
            const char* value = nextValue("--name");
            if (value == nullptr) return 2;
            options.name = value;
        } else {
            std::fprintf(stderr, "error: unknown option: %s\n", argument);
            printUsage(argv[0]);
            return 2;
        }
    }

    if (options.pin == 0) {
        std::fprintf(stderr, "error: --pin is required (see server startup output)\n");
        printUsage(argv[0]);
        return 2;
    }

    net::WinsockGuard winsock;
    if (!winsock.ok()) {
        std::fprintf(stderr, "error: socket startup failed: %s\n",
                     net::errorString(winsock.error()).c_str());
        return 1;
    }

    std::string error;
    const socket_t socket = dialTcp(options.host, options.port, 4000, error);
    if (socket == kInvalidSocket) {
        std::fprintf(stderr, "error: cannot connect to %s:%u (%s)\n",
                     options.host.c_str(), static_cast<unsigned>(options.port),
                     error.c_str());
        return 1;
    }

    Connection connection(socket);

    mctl::Frame frame;
    if (!expect(connection, mctl::MsgType::ServerHello, frame, 4000,
                "SERVER_HELLO")) {
        return 1;
    }

    mctl::ServerHelloInfo hello;
    if (!mctl::parseServerHello(frame, hello)) {
        std::fprintf(stderr, "error: malformed SERVER_HELLO\n");
        return 1;
    }
    if (hello.version != mctl::kProtocolVersion) {
        std::fprintf(stderr, "error: server protocol %u, client speaks %u\n",
                     static_cast<unsigned>(hello.version),
                     static_cast<unsigned>(mctl::kProtocolVersion));
        return 1;
    }

    std::printf("[sim] connected to \"%s\" at %s:%u\n", hello.name.c_str(),
                options.host.c_str(), static_cast<unsigned>(options.port));

    if (!connection.sendFrame(mctl::makeAuth(options.pin, options.name))) {
        std::fprintf(stderr, "error: could not send AUTH\n");
        return 1;
    }
    if (!expect(connection, mctl::MsgType::AuthResult, frame, 5000,
                "AUTH_RESULT")) {
        return 1;
    }

    mctl::AuthResultInfo auth;
    if (!mctl::parseAuthResult(frame, auth)) {
        std::fprintf(stderr, "error: malformed AUTH_RESULT\n");
        return 1;
    }
    if (!auth.ok) {
        std::fprintf(stderr, "error: pairing rejected: %s (%u attempts left)\n",
                     auth.message.c_str(),
                     static_cast<unsigned>(auth.attemptsLeft));
        return 3;
    }

    std::printf("[sim] paired as \"%s\"\n", options.name.c_str());
    std::printf("[sim] running input script (run the server with --dry-run to "
                "avoid moving the real cursor)\n");

    if (!moveBy(connection, 240, 0, 24)) return 1;
    if (!click(connection, static_cast<uint8_t>(mctl::MouseButton::Left))) {
        return 1;
    }
    std::printf("[sim] move right + left click\n");

    if (!moveBy(connection, 0, 180, 18)) return 1;
    if (!click(connection, static_cast<uint8_t>(mctl::MouseButton::Right))) {
        return 1;
    }
    std::printf("[sim] move down + right click\n");

    if (!connection.sendFrame(mctl::makeWheel(0, 3))) return 1;
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    if (!connection.sendFrame(mctl::makeWheel(0, -2))) return 1;
    std::printf("[sim] scroll 3 notches up, 2 down\n");

    if (!connection.sendFrame(
            mctl::makeButton(mctl::MouseButton::Left, true))) {
        return 1;
    }
    if (!moveBy(connection, 200, 0, 20)) return 1;
    if (!connection.sendFrame(
            mctl::makeButton(mctl::MouseButton::Left, false))) {
        return 1;
    }
    std::printf("[sim] drag selection 200px\n");

    for (uint32_t token = 1; token <= 3; ++token) {
        int roundTrip = 0;
        if (!measurePing(connection, token, roundTrip)) return 1;
        std::printf("[sim] ping %u: %d ms\n", static_cast<unsigned>(token),
                    roundTrip);
    }

    std::printf("[sim] done\n");
    return 0;
}
