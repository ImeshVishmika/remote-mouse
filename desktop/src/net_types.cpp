#include "net_types.h"

#include <cstdio>
#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/time.h>
#endif

#ifndef TCP_NODELAY
#define TCP_NODELAY 0x0001
#endif

namespace net {

WinsockGuard::WinsockGuard() {
#ifdef _WIN32
    WSADATA data{};
    error_ = WSAStartup(MAKEWORD(2, 2), &data);
    ok_ = (error_ == 0);
#else
    ok_ = true;
#endif
}

WinsockGuard::~WinsockGuard() {
#ifdef _WIN32
    if (ok_) WSACleanup();
#endif
}

int lastError() {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

std::string errorString(int code) {
#ifdef _WIN32
    char* buffer = nullptr;
    const DWORD length = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, static_cast<DWORD>(code), 0,
        reinterpret_cast<char*>(&buffer), 0, nullptr);

    std::string text;
    if (length != 0 && buffer != nullptr) {
        text.assign(buffer, length);
        while (!text.empty() &&
               (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) {
            text.pop_back();
        }
    }
    if (buffer != nullptr) LocalFree(buffer);

    char prefix[32];
    std::snprintf(prefix, sizeof(prefix), "socket error %d", code);
    if (text.empty()) return prefix;
    return std::string(prefix) + ": " + text;
#else
    return std::strerror(code);
#endif
}

bool isWouldBlock(int code) {
#ifdef _WIN32
    return code == WSAEWOULDBLOCK || code == WSAEINTR;
#else
    return code == EWOULDBLOCK || code == EAGAIN || code == EINTR;
#endif
}

bool setNonBlocking(socket_t socket, bool enabled) {
#ifdef _WIN32
    u_long mode = enabled ? 1ul : 0ul;
    return ioctlsocket(socket, FIONBIO, &mode) == 0;
#else
    const int flags = fcntl(socket, F_GETFL, 0);
    if (flags < 0) return false;
    const int updated = enabled ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    return fcntl(socket, F_SETFL, updated) == 0;
#endif
}

bool setNoDelay(socket_t socket) {
    const int enabled = 1;
    return setsockopt(socket, IPPROTO_TCP, TCP_NODELAY,
                      reinterpret_cast<const char*>(&enabled),
                      static_cast<socklen_type>(sizeof(enabled))) == 0;
}

bool setSendTimeout(socket_t socket, int milliseconds) {
#ifdef _WIN32
    const DWORD timeout = static_cast<DWORD>(milliseconds);
    return setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO,
                      reinterpret_cast<const char*>(&timeout),
                      static_cast<int>(sizeof(timeout))) == 0;
#else
    timeval timeout{};
    timeout.tv_sec = milliseconds / 1000;
    timeout.tv_usec = (milliseconds % 1000) * 1000;
    return setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0;
#endif
}

void closeSocket(socket_t socket) {
    if (socket == kInvalidSocket) return;
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

void shutdownSocket(socket_t socket) {
    if (socket == kInvalidSocket) return;
#ifdef _WIN32
    shutdown(socket, SD_BOTH);
#else
    shutdown(socket, SHUT_RDWR);
#endif
}

std::string peerToString(const sockaddr_in& address) {
    char buffer[INET_ADDRSTRLEN] = {0};
    if (inet_ntop(AF_INET, &address.sin_addr, buffer, sizeof(buffer)) == nullptr) {
        std::snprintf(buffer, sizeof(buffer), "?");
    }
    char result[INET_ADDRSTRLEN + 8];
    std::snprintf(result, sizeof(result), "%s:%u", buffer,
                  static_cast<unsigned>(ntohs(address.sin_port)));
    return result;
}

}  // namespace net
