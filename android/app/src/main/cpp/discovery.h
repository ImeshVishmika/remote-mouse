#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "protocol.h"

struct DiscoveredServer {
    std::string ip;
    uint16_t port = 0;
    std::string name;
    uint64_t lastSeenMs = 0;
};

// Sends a broadcast probe every kProbeIntervalMs and collects the answers.
// Servers that stop answering disappear after kServerTtlMs. Everything is
// non-blocking and driven from the render loop by poll().
class DiscoveryClient {
public:
    ~DiscoveryClient();

    void start(uint16_t discoveryPort = mctl::kDefaultDiscoveryPort);
    void stop();
    void poll();

    bool running() const { return socket_ >= 0; }
    const std::vector<DiscoveredServer>& servers() const { return servers_; }

private:
    void sendProbe(uint64_t now);
    void receiveAll(uint64_t now);
    void expire(uint64_t now);

    int socket_ = -1;
    uint16_t port_ = 0;
    uint64_t lastProbeMs_ = 0;
    std::vector<DiscoveredServer> servers_;
};
