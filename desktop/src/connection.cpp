#include "connection.h"

#include <algorithm>
#include <cstring>

#ifndef _WIN32
#include <cerrno>
#endif

namespace {

enum class WaitResult {
    Ready,
    Timeout,
    Error,
};

WaitResult waitFor(socket_t socket, bool forWrite, int timeoutMs) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(socket, &set);

    timeval timeout{};
    timeout.tv_sec = timeoutMs / 1000;
    timeout.tv_usec = (timeoutMs % 1000) * 1000;

#ifdef _WIN32
    const int ready = select(0, forWrite ? nullptr : &set,
                             forWrite ? &set : nullptr, nullptr, &timeout);
#else
    const int ready = select(socket + 1, forWrite ? nullptr : &set,
                             forWrite ? &set : nullptr, nullptr, &timeout);
#endif

    if (ready == 0) return WaitResult::Timeout;
    if (ready == SOCKET_ERROR) return WaitResult::Error;
    return WaitResult::Ready;
}

}  // namespace

Connection::Connection(socket_t socket) : socket_(socket) {
    net::setNoDelay(socket_);
    net::setSendTimeout(socket_, 2000);
}

Connection::~Connection() {
    net::closeSocket(socket_);
    socket_ = kInvalidSocket;
}

bool Connection::sendFrame(const std::vector<uint8_t>& frame) {
    size_t offset = 0;
    while (offset < frame.size()) {
        const size_t remaining = frame.size() - offset;
        const int chunk = static_cast<int>(std::min<size_t>(remaining, 16 * 1024));
        const int sent =
            send(socket_, reinterpret_cast<const char*>(frame.data() + offset),
                 chunk, 0);

        if (sent <= 0) {
            if (sent == SOCKET_ERROR && net::isWouldBlock(net::lastError())) {
                if (waitFor(socket_, true, 1000) != WaitResult::Ready) return false;
                continue;
            }
            return false;
        }
        offset += static_cast<size_t>(sent);
    }
    return true;
}

ReadResult Connection::readFrame(mctl::Frame& out, int timeoutMs,
                                 std::string* error) {
    for (;;) {
        const mctl::ParseStatus status = parser_.next(out);
        if (status == mctl::ParseStatus::Ok) return ReadResult::Frame;

        if (status != mctl::ParseStatus::NeedMore) {
            if (error != nullptr) {
                switch (status) {
                    case mctl::ParseStatus::BadMagic:
                        *error = "bad frame magic";
                        break;
                    case mctl::ParseStatus::BadVersion:
                        *error = "unsupported protocol version";
                        break;
                    case mctl::ParseStatus::BadLength:
                        *error = "payload exceeds protocol limit";
                        break;
                    default:
                        *error = "malformed frame";
                        break;
                }
            }
            return ReadResult::Error;
        }

        if (timeoutMs <= 0) return ReadResult::Timeout;

        const WaitResult wait = waitFor(socket_, false, timeoutMs);
        if (wait == WaitResult::Timeout) return ReadResult::Timeout;
        if (wait == WaitResult::Error) {
            if (error != nullptr) *error = net::errorString(net::lastError());
            return ReadResult::Error;
        }

        uint8_t buffer[4096];
        const int received =
            recv(socket_, reinterpret_cast<char*>(buffer), sizeof(buffer), 0);

        if (received == 0) return ReadResult::Closed;
        if (received == SOCKET_ERROR) {
            const int code = net::lastError();
            if (net::isWouldBlock(code)) continue;
            if (error != nullptr) *error = net::errorString(code);
            return ReadResult::Error;
        }

        parser_.push(buffer, static_cast<size_t>(received));
    }
}

void Connection::shutdownBoth() {
    net::shutdownSocket(socket_);
}

socket_t dialTcp(const std::string& host, uint16_t port, int timeoutMs,
                 std::string& error) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
        error = "not an IPv4 address: " + host;
        return kInvalidSocket;
    }

    const socket_t socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket == kInvalidSocket) {
        error = net::errorString(net::lastError());
        return kInvalidSocket;
    }

    if (!net::setNonBlocking(socket, true)) {
        error = "could not switch socket to non-blocking mode";
        net::closeSocket(socket);
        return kInvalidSocket;
    }

    const int result =
        connect(socket, reinterpret_cast<const sockaddr*>(&address),
                static_cast<socklen_type>(sizeof(address)));

    if (result == SOCKET_ERROR) {
        const int code = net::lastError();
#ifdef _WIN32
        const bool inProgress = (code == WSAEWOULDBLOCK);
#else
        const bool inProgress = (code == EINPROGRESS);
#endif
        if (!inProgress) {
            error = net::errorString(code);
            net::closeSocket(socket);
            return kInvalidSocket;
        }

        const WaitResult wait = waitFor(socket, true, timeoutMs);
        if (wait != WaitResult::Ready) {
            error = (wait == WaitResult::Timeout) ? "connection timed out"
                                                 : net::errorString(net::lastError());
            net::closeSocket(socket);
            return kInvalidSocket;
        }

        int socketError = 0;
        socklen_type length = static_cast<socklen_type>(sizeof(socketError));
        if (getsockopt(socket, SOL_SOCKET, SO_ERROR,
                       reinterpret_cast<char*>(&socketError), &length) != 0) {
            error = net::errorString(net::lastError());
            net::closeSocket(socket);
            return kInvalidSocket;
        }
        if (socketError != 0) {
            error = net::errorString(socketError);
            net::closeSocket(socket);
            return kInvalidSocket;
        }
    }

    net::setNonBlocking(socket, false);
    net::setNoDelay(socket);
    return socket;
}
