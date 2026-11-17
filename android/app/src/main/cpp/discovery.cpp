#include "discovery.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>

namespace {

constexpr uint64_t kProbeIntervalMs = 1500;
constexpr uint64_t kServerTtlMs = 5000;

uint64_t nowMs() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
            .count());
}

}  // namespace

DiscoveryClient::~DiscoveryClient() {
    stop();
}

void DiscoveryClient::start(uint16_t discoveryPort) {
    if (socket_ >= 0 && port_ == discoveryPort) return;

    stop();
    port_ = discoveryPort;

    socket_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_ < 0) {
        port_ = 0;
        return;
    }

    const int flags = fcntl(socket_, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(socket_, F_SETFL, flags | O_NONBLOCK);
    }

    const int enable = 1;
    setsockopt(socket_, SOL_SOCKET, SO_BROADCAST, &enable, sizeof(enable));

    // Bind to an ephemeral port: replies come back to the sender address.
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = 0;

    if (bind(socket_, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) !=
        0) {
        ::close(socket_);
        socket_ = -1;
        port_ = 0;
        return;
    }

    lastProbeMs_ = 0;
    servers_.clear();
}

void DiscoveryClient::stop() {
    if (socket_ >= 0) {
        ::close(socket_);
        socket_ = -1;
    }
    port_ = 0;
    lastProbeMs_ = 0;
    servers_.clear();
}

void DiscoveryClient::poll() {
    if (socket_ < 0) return;

    const uint64_t now = nowMs();

    if (lastProbeMs_ == 0 || now - lastProbeMs_ >= kProbeIntervalMs) {
        sendProbe(now);
    }

    receiveAll(now);
    expire(now);
}

void DiscoveryClient::sendProbe(uint64_t now) {
    const std::vector<uint8_t> request = mctl::makeDiscoveryRequest();

    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_port = htons(port_);

    // The LAN broadcast everybody listens on...
    target.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    sendto(socket_, request.data(), request.size(), 0,
           reinterpret_cast<const sockaddr*>(&target), sizeof(target));

    // ...plus loopback so an emulator or a server on this device still shows up.
    target.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sendto(socket_, request.data(), request.size(), 0,
           reinterpret_cast<const sockaddr*>(&target), sizeof(target));

    lastProbeMs_ = now;
}

void DiscoveryClient::receiveAll(uint64_t now) {
    bool changed = false;

    for (;;) {
        uint8_t buffer[512];
        sockaddr_in from{};
        socklen_t length = sizeof(from);

        const ssize_t received =
            recvfrom(socket_, buffer, sizeof(buffer), 0,
                     reinterpret_cast<sockaddr*>(&from), &length);
        if (received <= 0) break;

        mctl::DiscoveryResponse response;
        if (!mctl::parseDiscoveryResponse(buffer,
                                          static_cast<size_t>(received),
                                          response)) {
            continue;
        }
        if (response.tcpPort == 0) continue;

        char ip[INET_ADDRSTRLEN] = {0};
        if (inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip)) == nullptr) {
            continue;
        }

        bool found = false;
        for (DiscoveredServer& server : servers_) {
            if (server.ip == ip && server.port == response.tcpPort) {
                server.name = response.name;
                server.lastSeenMs = now;
                found = true;
                break;
            }
        }

        if (!found) {
            DiscoveredServer server;
            server.ip = ip;
            server.port = response.tcpPort;
            server.name = response.name;
            server.lastSeenMs = now;
            servers_.push_back(server);
            changed = true;
        }
    }

    if (changed) {
        // Keep the list order stable so cards do not jump under the finger.
        std::sort(servers_.begin(), servers_.end(),
                  [](const DiscoveredServer& a, const DiscoveredServer& b) {
                      if (a.ip != b.ip) return a.ip < b.ip;
                      return a.port < b.port;
                  });
    }
}

void DiscoveryClient::expire(uint64_t now) {
    servers_.erase(
        std::remove_if(servers_.begin(), servers_.end(),
                       [now](const DiscoveredServer& server) {
                           return now > server.lastSeenMs &&
                                  (now - server.lastSeenMs) > kServerTtlMs;
                       }),
        servers_.end());
}
