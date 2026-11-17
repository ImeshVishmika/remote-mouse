#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "protocol.h"

struct NetEvent {
    enum class Type {
        AuthNeeded,
        AuthOk,
        AuthFailed,
        Closed,
        Failed,
        Latency,
    };

    Type type = Type::Closed;
    std::string text;
    int value = 0;
};

// Non-blocking TCP client: connect -> SERVER_HELLO -> AUTH -> ready.
// All socket work happens in poll(), driven by the render loop.
class NetClient {
public:
    ~NetClient();

    void connectTo(const std::string& ip, uint16_t port,
                   const std::string& deviceName);
    void disconnect(const std::string& reason = std::string());
    void poll();
    void resume();
    void submitPin(uint32_t pin);

    void sendMove(int dx, int dy);
    void sendButton(uint8_t button, bool pressed);
    void sendWheel(int dx, int dy);

    bool active() const { return fd_ >= 0; }
    bool ready() const;
    int latencyMs() const { return latencyMs_; }
    const std::string& serverName() const { return serverName_; }
    uint8_t attemptsLeft() const { return attemptsLeft_; }
    uint32_t pinInUse() const { return pinInUse_; }

    std::vector<NetEvent> takeEvents();

private:
    enum class State {
        Idle,
        Connecting,
        Handshake,
        Ready,
    };

    void reset();
    void closeSocket();
    void fail(const std::string& message);
    void pushEvent(NetEvent::Type type, std::string text = std::string(),
                   int value = 0);
    void queue(const std::vector<uint8_t>& frame);
    void flush();
    void readAvailable();
    void processFrame(const mctl::Frame& frame);
    void sendPing();

    int fd_ = -1;
    State state_ = State::Idle;
    mctl::FrameParser parser_;
    std::vector<uint8_t> outbox_;
    size_t outOffset_ = 0;

    std::string deviceName_;
    std::string serverName_;
    uint32_t pinInUse_ = 0;
    uint8_t attemptsLeft_ = 0;

    uint64_t deadlineMs_ = 0;
    uint64_t lastPingMs_ = 0;
    uint64_t lastPongMs_ = 0;
    uint32_t pingToken_ = 0;
    uint32_t pendingPingToken_ = 0;
    uint64_t pendingPingMs_ = 0;
    int latencyMs_ = -1;

    std::deque<NetEvent> events_;
};
