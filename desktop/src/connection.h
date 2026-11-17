#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "net_types.h"
#include "protocol.h"

enum class ReadResult {
    Frame,
    Timeout,
    Closed,
    Error,
};

// Buffered, framed TCP connection. One instance is owned by the thread that
// services the socket.
class Connection {
public:
    explicit Connection(socket_t socket);
    ~Connection();

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    socket_t handle() const { return socket_; }
    bool sendFrame(const std::vector<uint8_t>& frame);

    // Waits up to timeoutMs for one complete frame.
    // Timeout leaves any partial frame buffered for the next call.
    ReadResult readFrame(mctl::Frame& out, int timeoutMs,
                         std::string* error = nullptr);

    void shutdownBoth();

private:
    socket_t socket_;
    mctl::FrameParser parser_;
};

// Resolves host and connects with an overall deadline.
// Returns kInvalidSocket and fills error on failure.
socket_t dialTcp(const std::string& host, uint16_t port, int timeoutMs,
                 std::string& error);
