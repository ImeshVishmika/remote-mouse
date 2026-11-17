#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "config.h"
#include "connection.h"
#include "mouse.h"
#include "net_types.h"

class DiscoveryResponder;

// Single-device TCP server: accepts one phone, pairs it with a PIN, then turns
// framed mouse messages into Windows input events.
class Server {
public:
    explicit Server(ServerConfig config);
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    int run();
    void requestStop();

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
    socket_t listener_ = kInvalidSocket;
    std::thread consoleThread_;
    std::thread discoveryThread_;
    std::thread sessionThread_;
    std::unique_ptr<DiscoveryResponder> discovery_;

    std::atomic<bool> stop_{false};
    std::atomic<bool> busy_{false};
    std::atomic<bool> kick_{false};
    std::atomic<uint64_t> sessionCounter_{0};

    mutable std::mutex clientMutex_;
    socket_t client_ = kInvalidSocket;
    std::string clientPeer_;
};
