#include "server.h"

#include <chrono>
#include <cstdio>
#include <utility>
#include <vector>

#include "discovery.h"
#include "log.h"
#include "platform.h"
#include "protocol.h"

#ifdef _WIN32
#include <conio.h>
#endif

namespace {

bool pinMatches(uint32_t supplied, uint32_t expected) {
    // Keep the comparison free of early exits.
    volatile uint32_t difference = supplied ^ expected;
    return difference == 0;
}

unsigned msgTypeValue(mctl::MsgType type) {
    return static_cast<unsigned>(static_cast<uint8_t>(type));
}

}  // namespace

Server::Server(ServerConfig config)
    : config_(std::move(config)),
      mouse_(config_.dryRun, config_.sensitivity) {}

Server::~Server() {
    requestStop();
    if (sessionThread_.joinable()) sessionThread_.join();
    if (discoveryThread_.joinable()) discoveryThread_.join();
    if (discovery_) discovery_->close();
    if (consoleThread_.joinable()) consoleThread_.join();
    net::closeSocket(listener_);
    listener_ = kInvalidSocket;
}

void Server::requestStop() {
    if (!stop_.exchange(true)) {
        logx::event("Shutting down...");
    }
}

bool Server::stopping() const {
    return stop_.load() || platformQuitRequested();
}

int Server::run() {
    net::WinsockGuard winsock;
    if (!winsock.ok()) {
        logx::error("Winsock startup failed: %s",
                    net::errorString(winsock.error()).c_str());
        return 1;
    }

    std::string error;
    if (!openListener(error)) {
        logx::error("%s", error.c_str());
        return 1;
    }

    if (config_.discovery) {
        discovery_ = std::make_unique<DiscoveryResponder>(
            config_.name, config_.port, config_.discoveryPort, config_.verbose);
        if (!discovery_->start(error)) {
            logx::warn("%s (discovery disabled)", error.c_str());
            discovery_.reset();
        } else {
            discoveryThread_ =
                std::thread([this]() { discovery_->run(stop_); });
        }
    }

    printBanner();
    consoleThread_ = std::thread(&Server::consoleLoop, this);

    acceptLoop();

    requestStop();
    if (sessionThread_.joinable()) sessionThread_.join();
    if (discoveryThread_.joinable()) discoveryThread_.join();
    if (discovery_) discovery_->close();
    if (consoleThread_.joinable()) consoleThread_.join();

    net::closeSocket(listener_);
    listener_ = kInvalidSocket;
    logx::info("Server stopped.");
    return 0;
}

bool Server::openListener(std::string& error) {
    listener_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener_ == kInvalidSocket) {
        error = "socket() failed: " + net::errorString(net::lastError());
        return false;
    }

#ifdef _WIN32
    BOOL exclusive = TRUE;
    setsockopt(listener_, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
               reinterpret_cast<const char*>(&exclusive),
               static_cast<socklen_type>(sizeof(exclusive)));
#else
    const int reuse = 1;
    setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&reuse),
               static_cast<socklen_type>(sizeof(reuse)));
#endif

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(config_.port);

    if (bind(listener_, reinterpret_cast<const sockaddr*>(&address),
             static_cast<socklen_type>(sizeof(address))) != 0) {
        error = "bind tcp/" + std::to_string(config_.port) + " failed: " +
                net::errorString(net::lastError());
        return false;
    }

    if (listen(listener_, 4) != 0) {
        error = "listen() failed: " + net::errorString(net::lastError());
        return false;
    }

    return true;
}

void Server::acceptLoop() {
    logx::event("Waiting for devices on TCP %u", static_cast<unsigned>(config_.port));

    while (!stopping()) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(listener_, &readSet);

        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 500 * 1000;

#ifdef _WIN32
        const int ready = select(0, &readSet, nullptr, nullptr, &timeout);
#else
        const int ready = select(listener_ + 1, &readSet, nullptr, nullptr, &timeout);
#endif

        if (ready == SOCKET_ERROR) {
            if (!stopping()) {
                logx::warn("select() failed: %s",
                           net::errorString(net::lastError()).c_str());
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
            continue;
        }
        if (ready == 0) continue;

        sockaddr_in address{};
        socklen_type length = static_cast<socklen_type>(sizeof(address));
        const socket_t client =
            accept(listener_, reinterpret_cast<sockaddr*>(&address), &length);
        if (client == kInvalidSocket) {
            if (!stopping()) {
                logx::warn("accept() failed: %s",
                           net::errorString(net::lastError()).c_str());
            }
            continue;
        }

        const std::string peer = net::peerToString(address);

        if (busy_.load()) {
            Connection rejected(client);
            rejected.sendFrame(mctl::makeError(
                mctl::ErrorCode::Busy, "Another device is already connected"));
            logx::warn("Rejected %s: another device is already connected",
                       peer.c_str());
            continue;
        }

        busy_.store(true);
        kick_.store(false);
        if (sessionThread_.joinable()) sessionThread_.join();
        sessionThread_ = std::thread(&Server::sessionLoop, this, client, peer);
    }
}

void Server::sessionLoop(socket_t client, const std::string& peer) {
    {
        std::lock_guard<std::mutex> lock(clientMutex_);
        client_ = client;
        clientPeer_ = peer;
    }

    Connection connection(client);
    const uint64_t session = ++sessionCounter_;

    logx::event("Device connected: %s (session %llu)", peer.c_str(),
                static_cast<unsigned long long>(session));
    logx::info("Enter PIN %s on the phone to pair.", formatPin(config_.pin).c_str());

    mouse_.releaseAll();
    if (pairDevice(connection, peer)) {
        pumpInput(connection, peer);
    }
    // Safety: never leave a button latched if the phone vanished mid-drag.
    mouse_.releaseAll();

    logx::event("Session %llu ended (%s)",
                static_cast<unsigned long long>(session), peer.c_str());

    {
        std::lock_guard<std::mutex> lock(clientMutex_);
        if (client_ == client) {
            client_ = kInvalidSocket;
            clientPeer_.clear();
        }
    }
    busy_.store(false);
    kick_.store(false);
}

bool Server::pairDevice(Connection& connection, const std::string& peer) {
    if (!connection.sendFrame(
            mctl::makeServerHello(mctl::kMaxAuthAttempts, config_.name))) {
        logx::warn("Could not send server hello to %s", peer.c_str());
        return false;
    }

    uint8_t attempts = mctl::kMaxAuthAttempts;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(90);

    while (!stopping() && !kick_.load()) {
        if (std::chrono::steady_clock::now() > deadline) {
            connection.sendFrame(
                mctl::makeAuthResult(false, 0, "Pairing timed out"));
            logx::warn("Pairing timed out for %s", peer.c_str());
            return false;
        }

        mctl::Frame frame;
        std::string error;
        const ReadResult result = connection.readFrame(frame, 500, &error);

        if (result == ReadResult::Timeout) continue;
        if (result == ReadResult::Closed) {
            logx::info("%s closed the connection during pairing", peer.c_str());
            return false;
        }
        if (result == ReadResult::Error) {
            logx::warn("Read error from %s: %s", peer.c_str(), error.c_str());
            return false;
        }

        if (frame.type == mctl::MsgType::Ping) {
            uint32_t token = 0;
            if (mctl::parseToken(frame, token)) {
                connection.sendFrame(mctl::makePong(token));
            }
            continue;
        }

        if (frame.type != mctl::MsgType::Auth) {
            connection.sendFrame(mctl::makeError(
                mctl::ErrorCode::BadProtocol, "Expected AUTH frame"));
            logx::warn("Expected AUTH from %s, got message 0x%02X", peer.c_str(),
                       msgTypeValue(frame.type));
            return false;
        }

        mctl::AuthInfo info;
        if (!mctl::parseAuth(frame, info)) {
            connection.sendFrame(mctl::makeError(
                mctl::ErrorCode::BadProtocol, "Malformed AUTH frame"));
            return false;
        }

        if (pinMatches(info.pin, config_.pin)) {
            connection.sendFrame(mctl::makeAuthResult(
                true, attempts, "Paired with " + config_.name));
            logx::event("Paired with \"%s\" from %s", info.deviceName.c_str(),
                        peer.c_str());
            return true;
        }

        --attempts;
        const bool lastAttempt = (attempts == 0);
        connection.sendFrame(mctl::makeAuthResult(
            false, attempts, lastAttempt ? "Too many attempts" : "Wrong PIN"));
        logx::warn("Wrong PIN from %s (%u attempts left)", peer.c_str(),
                   static_cast<unsigned>(attempts));

        if (lastAttempt) {
            logx::warn("Pairing blocked for %s", peer.c_str());
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(750));
    }

    return false;
}

void Server::pumpInput(Connection& connection, const std::string& peer) {
    using Clock = std::chrono::steady_clock;

    const auto started = Clock::now();
    auto lastActivity = started;
    uint64_t packets = 0;
    uint64_t moves = 0;
    uint64_t buttons = 0;
    uint64_t wheels = 0;

    while (!stopping() && !kick_.load()) {
        mctl::Frame frame;
        std::string error;
        const ReadResult result = connection.readFrame(frame, 1000, &error);

        if (result == ReadResult::Timeout) {
            if (Clock::now() - lastActivity > std::chrono::seconds(20)) {
                logx::warn("%s went silent; dropping the session", peer.c_str());
                break;
            }
            continue;
        }
        if (result == ReadResult::Closed) {
            logx::info("%s disconnected", peer.c_str());
            break;
        }
        if (result == ReadResult::Error) {
            logx::warn("Read error from %s: %s", peer.c_str(), error.c_str());
            break;
        }

        lastActivity = Clock::now();
        ++packets;

        switch (frame.type) {
            case mctl::MsgType::Move: {
                int16_t dx = 0;
                int16_t dy = 0;
                if (!mctl::parseMove(frame, dx, dy)) break;
                mouse_.moveRelative(dx, dy);
                ++moves;
                if (config_.verbose && (dx != 0 || dy != 0)) {
                    logx::info("move %d,%d", static_cast<int>(dx),
                               static_cast<int>(dy));
                }
                break;
            }
            case mctl::MsgType::Button: {
                mctl::ButtonInfo info;
                if (!mctl::parseButton(frame, info)) break;
                mouse_.setButton(info.button, info.pressed);
                ++buttons;
                if (config_.verbose) {
                    logx::info("button %u %s", static_cast<unsigned>(info.button),
                               info.pressed ? "down" : "up");
                }
                break;
            }
            case mctl::MsgType::Wheel: {
                int16_t dx = 0;
                int16_t dy = 0;
                if (!mctl::parseWheel(frame, dx, dy)) break;
                mouse_.wheel(dx, dy);
                ++wheels;
                if (config_.verbose) {
                    logx::info("wheel %d,%d", static_cast<int>(dx),
                               static_cast<int>(dy));
                }
                break;
            }
            case mctl::MsgType::Ping: {
                uint32_t token = 0;
                if (mctl::parseToken(frame, token)) {
                    connection.sendFrame(mctl::makePong(token));
                }
                break;
            }
            default:
                break;
        }
    }

    const double seconds =
        std::chrono::duration<double>(Clock::now() - started).count();
    logx::event(
        "Session summary: %.1fs, %llu packets, %llu moves, %llu button events, "
        "%llu wheel notches",
        seconds, static_cast<unsigned long long>(packets),
        static_cast<unsigned long long>(moves),
        static_cast<unsigned long long>(buttons),
        static_cast<unsigned long long>(wheels));
}

void Server::consoleLoop() {
    while (!stopping()) {
#ifdef _WIN32
        if (_kbhit()) {
            const int key = _getch();
            switch (key) {
                case 'q':
                case 'Q':
                    requestStop();
                    break;
                case 'p':
                case 'P':
                    printPin();
                    break;
                case 's':
                case 'S':
                    printStatus();
                    break;
                case 'k':
                case 'K':
                    kickClient();
                    break;
                case 'h':
                case 'H':
                case '?':
                    printHelp();
                    break;
                default:
                    break;
            }
        }
#endif
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
    }
}

void Server::printBanner() const {
    const std::string discovery =
        config_.discovery ? "udp/" + std::to_string(config_.discoveryPort)
                          : std::string("disabled");

    logx::info("Remote Mouse server ready");
    logx::info("  Name        : %s", config_.name.c_str());
    logx::info("  TCP port    : %u", static_cast<unsigned>(config_.port));
    logx::info("  Discovery   : %s", discovery.c_str());

    const std::vector<std::string> addresses = platformLocalIpv4();
    if (addresses.empty()) {
        logx::info("  Local IPv4  : unknown (use `ipconfig` to find it)");
    } else {
        std::string joined;
        for (size_t i = 0; i < addresses.size(); ++i) {
            if (i != 0) joined += ", ";
            joined += addresses[i];
        }
        logx::info("  Local IPv4  : %s", joined.c_str());
        if (!config_.discovery) {
            logx::info("  Manual entry: type one of those addresses in the app");
        }
    }

    logx::info("  Cursor speed: %.2fx%s", static_cast<double>(config_.sensitivity),
               config_.dryRun ? "  [DRY RUN - no real input]" : "");
    logx::event("PAIRING PIN : %s", formatPin(config_.pin).c_str());
    printHelp();
    logx::info(
        "If the phone cannot reach the PC, allow this program through the "
        "Windows Firewall (TCP/UDP %u).",
        static_cast<unsigned>(config_.port));
}

void Server::printPin() const {
    logx::event("PAIRING PIN : %s", formatPin(config_.pin).c_str());
}

void Server::printStatus() const {
    std::string state;
    {
        std::lock_guard<std::mutex> lock(clientMutex_);
        state = (client_ != kInvalidSocket) ? ("connected to " + clientPeer_)
                                            : "waiting for a device";
    }
    logx::info("Status: %s", state.c_str());
    logx::info("PIN %s | sensitivity %.2fx | %s", formatPin(config_.pin).c_str(),
               static_cast<double>(config_.sensitivity),
               config_.dryRun ? "dry run" : "live input");
}

void Server::printHelp() const {
    logx::info("Keys: q quit | p show PIN | s status | k kick device | h help");
}

void Server::kickClient() {
    std::lock_guard<std::mutex> lock(clientMutex_);
    if (client_ == kInvalidSocket) {
        logx::info("No device is currently connected.");
        return;
    }
    logx::warn("Kicking %s", clientPeer_.c_str());
    kick_.store(true);
    net::shutdownSocket(client_);
}
