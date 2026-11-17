#include "discovery.h"

#include <cstring>
#include <utility>

#include "log.h"
#include "protocol.h"

DiscoveryResponder::DiscoveryResponder(std::string serverName, uint16_t tcpPort,
                                       uint16_t discoveryPort, bool verbose)
    : name_(std::move(serverName)),
      tcpPort_(tcpPort),
      discoveryPort_(discoveryPort),
      verbose_(verbose) {}

DiscoveryResponder::~DiscoveryResponder() {
    close();
}

bool DiscoveryResponder::start(std::string& error) {
    socket_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ == kInvalidSocket) {
        error = net::errorString(net::lastError());
        return false;
    }

    const int enable = 1;
    setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&enable),
               static_cast<socklen_type>(sizeof(enable)));
    setsockopt(socket_, SOL_SOCKET, SO_BROADCAST,
               reinterpret_cast<const char*>(&enable),
               static_cast<socklen_type>(sizeof(enable)));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(discoveryPort_);

    if (bind(socket_, reinterpret_cast<const sockaddr*>(&address),
             static_cast<socklen_type>(sizeof(address))) != 0) {
        error = "bind udp/" + std::to_string(discoveryPort_) + " failed: " +
                net::errorString(net::lastError());
        net::closeSocket(socket_);
        socket_ = kInvalidSocket;
        return false;
    }

    return true;
}

void DiscoveryResponder::run(const std::atomic<bool>& stop) {
    const socket_t socket = socket_;
    if (socket == kInvalidSocket) return;

    logx::info("Discovery listening on udp/%u", static_cast<unsigned>(discoveryPort_));

    const std::vector<uint8_t> reply =
        mctl::makeDiscoveryResponse(tcpPort_, name_);

    while (!stop.load()) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(socket, &readSet);

        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 500 * 1000;

#ifdef _WIN32
        const int ready = select(0, &readSet, nullptr, nullptr, &timeout);
#else
        const int ready = select(socket + 1, &readSet, nullptr, nullptr, &timeout);
#endif
        if (ready <= 0) continue;

        sockaddr_in from{};
        socklen_type fromLength = static_cast<socklen_type>(sizeof(from));
        uint8_t buffer[64];
        const int received =
            recvfrom(socket, reinterpret_cast<char*>(buffer), sizeof(buffer), 0,
                     reinterpret_cast<sockaddr*>(&from), &fromLength);

        if (received <= 0) continue;
        if (!mctl::isDiscoveryRequest(buffer, static_cast<size_t>(received))) {
            continue;
        }

        sendto(socket, reinterpret_cast<const char*>(reply.data()),
               static_cast<int>(reply.size()), 0,
               reinterpret_cast<const sockaddr*>(&from), fromLength);

        if (verbose_) {
            logx::info("Discovery probe answered for %s",
                       net::peerToString(from).c_str());
        }
    }
}

void DiscoveryResponder::close() {
    if (socket_ != kInvalidSocket) {
        net::closeSocket(socket_);
        socket_ = kInvalidSocket;
    }
}
