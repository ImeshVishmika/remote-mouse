#include "net.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace {

uint64_t nowMs() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
            .count());
}

}  // namespace

NetClient::~NetClient() {
    closeSocket();
}

bool NetClient::ready() const {
    return state_ == State::Ready;
}

void NetClient::reset() {
    closeSocket();
    parser_.reset();
    outbox_.clear();
    outOffset_ = 0;
    state_ = State::Idle;
    serverName_.clear();
    pinInUse_ = 0;
    attemptsLeft_ = 0;
    deadlineMs_ = 0;
    lastPingMs_ = 0;
    lastPongMs_ = 0;
    pendingPingToken_ = 0;
    pendingPingMs_ = 0;
    latencyMs_ = -1;
}

void NetClient::connectTo(const std::string& ip, uint16_t port,
                          const std::string& deviceName) {
    reset();
    deviceName_ = deviceName;

    fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) {
        pushEvent(NetEvent::Type::Failed, "SOCKET ERROR");
        return;
    }

    const int flags = fcntl(fd_, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
    }

    const int one = 1;
    setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &address.sin_addr) != 1) {
        closeSocket();
        pushEvent(NetEvent::Type::Failed, "INVALID IPV4 ADDRESS");
        return;
    }

    const int result =
        ::connect(fd_, reinterpret_cast<const sockaddr*>(&address),
                  sizeof(address));

    if (result == 0) {
        state_ = State::Handshake;
        // Connected immediately: only the server hello is outstanding.
        deadlineMs_ = nowMs() + 15000;
    } else if (errno == EINPROGRESS) {
        state_ = State::Connecting;
        deadlineMs_ = nowMs() + 6000;
    } else {
        const std::string message =
            std::string("CONNECT FAILED: ") + std::strerror(errno);
        closeSocket();
        pushEvent(NetEvent::Type::Failed, message);
        return;
    }
}

void NetClient::disconnect(const std::string& reason) {
    closeSocket();
    state_ = State::Idle;
    parser_.reset();
    outbox_.clear();
    outOffset_ = 0;
    deadlineMs_ = 0;
    pendingPingToken_ = 0;
    pendingPingMs_ = 0;
    if (!reason.empty()) pushEvent(NetEvent::Type::Closed, reason);
}

void NetClient::poll() {
    if (fd_ < 0) return;

    flush();
    if (fd_ < 0) return;

    uint64_t now = nowMs();

    if (state_ == State::Connecting) {
        fd_set writeSet;
        FD_ZERO(&writeSet);
        FD_SET(fd_, &writeSet);

        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 0;

        const int ready = select(fd_ + 1, nullptr, &writeSet, nullptr, &timeout);
        if (ready > 0) {
            int socketError = 0;
            socklen_t length = sizeof(socketError);
            if (getsockopt(fd_, SOL_SOCKET, SO_ERROR, &socketError, &length) != 0) {
                fail(std::string("CONNECT FAILED: ") + std::strerror(errno));
                return;
            }
            if (socketError != 0) {
                fail(std::string("CONNECT FAILED: ") +
                     std::strerror(socketError));
                return;
            }
            state_ = State::Handshake;
            deadlineMs_ = now + 8000;
        } else if (now > deadlineMs_) {
            fail("CONNECTION TIMED OUT");
            return;
        }
    }

    readAvailable();
    if (fd_ < 0) return;

    now = nowMs();

    if (state_ == State::Handshake && deadlineMs_ != 0 && now > deadlineMs_) {
        fail("PAIRING TIMED OUT");
        return;
    }

    if (state_ == State::Ready) {
        if (lastPingMs_ == 0 || now - lastPingMs_ >= 2000) {
            sendPing();
            lastPingMs_ = now;
        }
        if (lastPongMs_ != 0 && now - lastPongMs_ > 12000) {
            fail("CONNECTION LOST");
        }
    }
}

void NetClient::resume() {
    if (!ready()) return;
    // The app was paused: the ping timestamps are stale, but the socket may
    // still be healthy. Give it a fresh grace period and probe right away.
    lastPongMs_ = nowMs();
    lastPingMs_ = 0;
}

void NetClient::submitPin(uint32_t pin) {
    if (!active()) return;
    pinInUse_ = pin;
    if (state_ == State::Handshake) {
        // The server also runs a pairing deadline; keep ours slightly behind
        // so its message ("Too many attempts") wins when both expire.
        deadlineMs_ = nowMs() + 120000;
    }
    queue(mctl::makeAuth(pin, deviceName_));
}

void NetClient::sendMove(int dx, int dy) {
    if (!ready() || (dx == 0 && dy == 0)) return;
    queue(mctl::makeMove(mctl::clampToI16(dx), mctl::clampToI16(dy)));
}

void NetClient::sendButton(uint8_t button, bool pressed) {
    if (!ready() || button < 1 || button > 3) return;
    queue(mctl::makeButton(static_cast<mctl::MouseButton>(button), pressed));
}

void NetClient::sendWheel(int dx, int dy) {
    if (!ready()) return;
    const int clampedX = std::max(-30, std::min(30, dx));
    const int clampedY = std::max(-30, std::min(30, dy));
    if (clampedX == 0 && clampedY == 0) return;
    queue(mctl::makeWheel(static_cast<int16_t>(clampedX),
                          static_cast<int16_t>(clampedY)));
}

std::vector<NetEvent> NetClient::takeEvents() {
    std::vector<NetEvent> events(events_.begin(), events_.end());
    events_.clear();
    return events;
}

void NetClient::closeSocket() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

void NetClient::fail(const std::string& message) {
    if (fd_ < 0) return;
    closeSocket();
    state_ = State::Idle;
    outbox_.clear();
    outOffset_ = 0;
    pushEvent(NetEvent::Type::Failed, message);
}

void NetClient::pushEvent(NetEvent::Type type, std::string text, int value) {
    NetEvent event;
    event.type = type;
    event.text = std::move(text);
    event.value = value;
    events_.push_back(event);
}

void NetClient::queue(const std::vector<uint8_t>& frame) {
    if (fd_ < 0) return;

    outbox_.insert(outbox_.end(), frame.begin(), frame.end());
    if (outbox_.size() - outOffset_ > 64 * 1024) {
        fail("NETWORK BACKLOG");
        return;
    }
    flush();
}

void NetClient::flush() {
    if (fd_ < 0) return;

    while (outOffset_ < outbox_.size()) {
        const ssize_t sent =
            ::send(fd_, outbox_.data() + outOffset_,
                   outbox_.size() - outOffset_, MSG_NOSIGNAL);

        if (sent > 0) {
            outOffset_ += static_cast<size_t>(sent);
            continue;
        }
        if (sent < 0 && errno == EINTR) continue;
        if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;

        fail(std::string("SEND FAILED: ") + std::strerror(errno));
        return;
    }

    outbox_.clear();
    outOffset_ = 0;
}

void NetClient::readAvailable() {
    if (fd_ < 0) return;

    for (;;) {
        uint8_t buffer[2048];
        const ssize_t received = ::recv(fd_, buffer, sizeof(buffer), 0);

        if (received > 0) {
            parser_.push(buffer, static_cast<size_t>(received));
            continue;
        }
        if (received == 0) {
            fail("SERVER CLOSED THE CONNECTION");
            return;
        }
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;

        fail(std::string("RECEIVE FAILED: ") + std::strerror(errno));
        return;
    }

    for (;;) {
        if (fd_ < 0) return;
        mctl::Frame frame;
        const mctl::ParseStatus status = parser_.next(frame);
        if (status == mctl::ParseStatus::NeedMore) return;
        if (status != mctl::ParseStatus::Ok) {
            fail("PROTOCOL ERROR");
            return;
        }
        processFrame(frame);
    }
}

void NetClient::processFrame(const mctl::Frame& frame) {
    switch (frame.type) {
        case mctl::MsgType::ServerHello: {
            mctl::ServerHelloInfo info;
            if (!mctl::parseServerHello(frame, info)) {
                fail("MALFORMED SERVER HELLO");
                return;
            }
            if (info.version != mctl::kProtocolVersion) {
                fail("PROTOCOL VERSION MISMATCH");
                return;
            }
            state_ = State::Handshake;
            deadlineMs_ = nowMs() + 120000;  // time to type the PIN
            serverName_ = info.name;
            attemptsLeft_ = info.attemptsLeft;
            pushEvent(NetEvent::Type::AuthNeeded, info.name, info.attemptsLeft);
            return;
        }
        case mctl::MsgType::AuthResult: {
            mctl::AuthResultInfo info;
            if (!mctl::parseAuthResult(frame, info)) {
                fail("MALFORMED AUTH RESULT");
                return;
            }
            attemptsLeft_ = info.attemptsLeft;
            if (info.ok) {
                state_ = State::Ready;
                lastPingMs_ = nowMs();
                lastPongMs_ = lastPingMs_;
                latencyMs_ = -1;
                pushEvent(NetEvent::Type::AuthOk);
            } else {
                pushEvent(NetEvent::Type::AuthFailed, info.message,
                          info.attemptsLeft);
                if (info.attemptsLeft == 0) {
                    closeSocket();
                    state_ = State::Idle;
                }
            }
            return;
        }
        case mctl::MsgType::Error: {
            mctl::ErrorInfo info;
            std::string message = "SERVER ERROR";
            if (mctl::parseError(frame, info) && !info.message.empty()) {
                message = info.message;
            }
            fail(message);
            return;
        }
        case mctl::MsgType::Pong: {
            uint32_t token = 0;
            if (!mctl::parseToken(frame, token)) return;

            const uint64_t now = nowMs();
            lastPongMs_ = now;

            if (token == pendingPingToken_ && pendingPingMs_ != 0 &&
                now >= pendingPingMs_) {
                latencyMs_ = static_cast<int>(now - pendingPingMs_);
                pushEvent(NetEvent::Type::Latency, std::string(), latencyMs_);
                pendingPingToken_ = 0;
            }
            return;
        }
        default:
            return;
    }
}

void NetClient::sendPing() {
    if (fd_ < 0) return;
    ++pingToken_;
    pendingPingToken_ = pingToken_;
    pendingPingMs_ = nowMs();
    queue(mctl::makePing(pingToken_));
}
