#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include "net_types.h"

// Answers UDP broadcast probes so phones can find the PC without typing an IP.
// run() blocks until stop is set; call close() to unblock select() early.
class DiscoveryResponder {
public:
    DiscoveryResponder(std::string serverName, uint16_t tcpPort,
                       uint16_t discoveryPort, bool verbose);
    ~DiscoveryResponder();

    DiscoveryResponder(const DiscoveryResponder&) = delete;
    DiscoveryResponder& operator=(const DiscoveryResponder&) = delete;

    bool start(std::string& error);
    void run(const std::atomic<bool>& stop);
    void close();

private:
    std::string name_;
    uint16_t tcpPort_;
    uint16_t discoveryPort_;
    bool verbose_;
    socket_t socket_ = kInvalidSocket;
};
