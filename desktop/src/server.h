#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "config.h"
#include "connection.h"
#include "keyboard.h"
#include "mouse.h"
#include "net_types.h"

class DiscoveryResponder;

// Single-device TCP server: accepts one phone, pairs it with a PIN, then turns
// framed mouse and keyboard messages into Windows input events.
// Start/stop, PIN changes and block-list management are thread-safe and can be
// driven from either the console or the GUI.
class Server {
public:
    explicit Server(ServerConfig config);
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // CLI entry point: starts, then blocks on the console loop until stopped.
    int run();
    void requestStop();

    // Start listening and accept connections. Returns false (with error) if the
    // listener cannot be opened.
    bool start(std::string& error);
    // Signal stop and join all threads. Safe to call from any thread.
    void stop();

    bool isRunning() const;

    // Human-readable one-line status (e.g. "Running - waiting for a device").
    std::string status() const;
    // Currently connected peer ("ip:port") or empty when idle.
    std::string connectedPeer() const;
    // Local IPv4 addresses for display.
    std::vector<std::string> localAddresses() const;

    // Pairing PIN (runtime changeable; takes effect for new connections).
    void setPin(uint32_t pin);
    uint32_t pin() const;

    // Block-list management.
    bool isBlocked(const std::string& peer) const;
    void blockPeer(const std::string& peer);
    void unblockPeer(const std::string& peer);
    std::vector<std::string> blockedPeers() const;

private:
    bool openListener(std::string& error);
    void acceptLoop();
    void sessionLoop(socket_t client, const std::string& peer);
    bool pairDevice(Connection& connection, const std::string& peer);
    void pumpInput(Connection& connection, const std::string& peer);
    void consoleLoop();
    void printBanner() const;
    void printPin() const;
    void printStatus() const;
    void printHelp() const;
    void kickClient();
    bool stopping() const;

    ServerConfig config_;
    MouseInjector mouse_;
    KeyboardInjector keyboard_;
    net::WinsockGuard winsock_;
    socket_t listener_ = kInvalidSocket;
    std::thread consoleThread_;
    std::thread discoveryThread_;
    std::thread acceptThread_;
    std::thread sessionThread_;
    std::unique_ptr<DiscoveryResponder> discovery_;

    std::atomic<bool> stop_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> busy_{false};
    std::atomic<bool> kick_{false};
    std::atomic<uint64_t> sessionCounter_{0};

    mutable std::mutex clientMutex_;
    socket_t client_ = kInvalidSocket;
    std::string clientPeer_;

    mutable std::mutex blockMutex_;
    std::unordered_set<std::string> blockedPeers_;
};
