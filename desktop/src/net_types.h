#pragma once

// Thin platform layer so the rest of the server never includes winsock directly.

#include <cstdint>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
using socklen_type = int;
constexpr socket_t kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
using socklen_type = socklen_t;
constexpr socket_t kInvalidSocket = -1;
#endif

#ifndef SOCKET_ERROR
#define SOCKET_ERROR (-1)
#endif

namespace net {

// Must be constructed once before any socket call on Windows.
class WinsockGuard {
public:
    WinsockGuard();
    ~WinsockGuard();
    WinsockGuard(const WinsockGuard&) = delete;
    WinsockGuard& operator=(const WinsockGuard&) = delete;

    bool ok() const { return ok_; }
    int error() const { return error_; }

private:
    bool ok_ = false;
    int error_ = 0;
};

int lastError();
std::string errorString(int code);
bool isWouldBlock(int code);

bool setNonBlocking(socket_t socket, bool enabled);
bool setNoDelay(socket_t socket);
bool setSendTimeout(socket_t socket, int milliseconds);
void closeSocket(socket_t socket);
void shutdownSocket(socket_t socket);

std::string peerToString(const sockaddr_in& address);

}  // namespace net
