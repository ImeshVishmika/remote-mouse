// Shared wire protocol for the Remote Mouse app and desktop server.
//
// Design goals:
//   * Trivially parseable on both ends (fixed 8 byte header + small payload).
//   * Little-endian on the wire, independent of host byte order.
//   * Explicit payload lengths so a receiver can resynchronise / validate.
//
// Every TCP message looks like:
//
//   offset  size  field
//   0       4     magic 'M','C','T','L'
//   4       1     protocol version (1)
//   5       1     message type (see MsgType)
//   6       2     payload length, little-endian
//   8       n     payload
//
// Discovery is a separate, unframed datagram exchange (see bottom of file).

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace mctl {

constexpr uint8_t kMagic[4] = {'M', 'C', 'T', 'L'};
constexpr uint8_t kProtocolVersion = 1;
constexpr size_t kHeaderSize = 8;
constexpr size_t kMaxPayload = 1024;
constexpr size_t kMaxStringLength = 255;

constexpr uint16_t kDefaultPort = 47824;
constexpr uint16_t kDefaultDiscoveryPort = 47824;
constexpr uint8_t kMaxAuthAttempts = 3;

enum class MsgType : uint8_t {
    ServerHello = 0x02,
    Auth = 0x03,
    AuthResult = 0x04,
    Move = 0x10,
    Button = 0x11,
    Wheel = 0x12,
    Ping = 0x13,
    Pong = 0x14,
    Text = 0x15,
    Key = 0x16,
    Error = 0x20,
};

enum class MouseButton : uint8_t {
    Left = 1,
    Right = 2,
    Middle = 3,
};

enum class KeyCode : uint8_t {
    Backspace = 1,
    Enter = 2,
    Tab = 3,
    Escape = 4,
    Up = 5,
    Down = 6,
    Left = 7,
    Right = 8,
};

enum class ErrorCode : uint8_t {
    Busy = 1,
    BadProtocol = 2,
    AuthFailed = 3,
    Timeout = 4,
    Unsupported = 5,
};

// ---------------------------------------------------------------------------
// Little-endian primitives
// ---------------------------------------------------------------------------

inline void putU16(uint8_t* out, uint16_t value) {
    out[0] = static_cast<uint8_t>(value & 0xFFu);
    out[1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
}

inline void putU32(uint8_t* out, uint32_t value) {
    out[0] = static_cast<uint8_t>(value & 0xFFu);
    out[1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
    out[2] = static_cast<uint8_t>((value >> 16) & 0xFFu);
    out[3] = static_cast<uint8_t>((value >> 24) & 0xFFu);
}

inline uint16_t getU16(const uint8_t* in) {
    return static_cast<uint16_t>(static_cast<uint16_t>(in[0]) |
                                 (static_cast<uint16_t>(in[1]) << 8));
}

inline uint32_t getU32(const uint8_t* in) {
    return static_cast<uint32_t>(in[0]) |
           (static_cast<uint32_t>(in[1]) << 8) |
           (static_cast<uint32_t>(in[2]) << 16) |
           (static_cast<uint32_t>(in[3]) << 24);
}

inline void putI16(uint8_t* out, int16_t value) {
    putU16(out, static_cast<uint16_t>(value));
}

inline int16_t getI16(const uint8_t* in) {
    return static_cast<int16_t>(getU16(in));
}

inline int16_t clampToI16(int value) {
    if (value > 32767) return 32767;
    if (value < -32768) return -32768;
    return static_cast<int16_t>(value);
}

// ---------------------------------------------------------------------------
// Encoding
// ---------------------------------------------------------------------------

class Writer {
public:
    explicit Writer(MsgType type, size_t reserve = 64) {
        buf_.reserve(kHeaderSize + reserve);
        buf_.insert(buf_.end(), kMagic, kMagic + 4);
        buf_.push_back(kProtocolVersion);
        buf_.push_back(static_cast<uint8_t>(type));
        buf_.push_back(0);
        buf_.push_back(0);
    }

    void u8(uint8_t value) { buf_.push_back(value); }

    void u16(uint16_t value) {
        uint8_t tmp[2];
        putU16(tmp, value);
        buf_.insert(buf_.end(), tmp, tmp + 2);
    }

    void u32(uint32_t value) {
        uint8_t tmp[4];
        putU32(tmp, value);
        buf_.insert(buf_.end(), tmp, tmp + 4);
    }

    void i16(int16_t value) { u16(static_cast<uint16_t>(value)); }

    void bytes(const uint8_t* data, size_t size) {
        buf_.insert(buf_.end(), data, data + size);
    }

    // Length-prefixed UTF-8 string. Truncated defensively at kMaxStringLength.
    void str(const std::string& value) {
        size_t size = value.size();
        if (size > kMaxStringLength) size = kMaxStringLength;
        u16(static_cast<uint16_t>(size));
        buf_.insert(buf_.end(), value.begin(),
                    value.begin() + static_cast<std::ptrdiff_t>(size));
    }

    std::vector<uint8_t> finish() {
        if (buf_.size() - kHeaderSize > kMaxPayload) {
            // Never truncate a frame header into something unparseable:
            // emit an empty payload instead. Callers keep messages small.
            buf_.resize(kHeaderSize);
        }
        putU16(&buf_[6], static_cast<uint16_t>(buf_.size() - kHeaderSize));
        return std::move(buf_);
    }

private:
    std::vector<uint8_t> buf_;
};

// ---------------------------------------------------------------------------
// Decoding
// ---------------------------------------------------------------------------

class Reader {
public:
    Reader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    bool u8(uint8_t& value) {
        if (!has(1)) return false;
        value = data_[pos_++];
        return true;
    }

    bool u16(uint16_t& value) {
        if (!has(2)) return false;
        value = getU16(data_ + pos_);
        pos_ += 2;
        return true;
    }

    bool u32(uint32_t& value) {
        if (!has(4)) return false;
        value = getU32(data_ + pos_);
        pos_ += 4;
        return true;
    }

    bool i16(int16_t& value) {
        uint16_t raw = 0;
        if (!u16(raw)) return false;
        value = static_cast<int16_t>(raw);
        return true;
    }

    bool str(std::string& value) {
        uint16_t size = 0;
        if (!u16(size)) return false;
        if (size > kMaxStringLength) return false;
        if (!has(size)) return false;
        value.assign(reinterpret_cast<const char*>(data_ + pos_), size);
        pos_ += size;
        return true;
    }

    size_t remaining() const { return size_ - pos_; }
    bool atEnd() const { return pos_ == size_; }

private:
    bool has(size_t count) const { return pos_ + count <= size_; }

    const uint8_t* data_;
    size_t size_;
    size_t pos_ = 0;
};

struct Frame {
    MsgType type = MsgType::Ping;
    std::vector<uint8_t> payload;
};

enum class ParseStatus {
    NeedMore,
    Ok,
    BadMagic,
    BadVersion,
    BadLength,
};

// Incremental TCP stream de-framer. Keeps a small buffer and hands out whole
// frames; callers just feed whatever recv() returned.
class FrameParser {
public:
    void push(const uint8_t* data, size_t size) {
        if (size != 0) buf_.insert(buf_.end(), data, data + size);
    }

    ParseStatus next(Frame& out) {
        if (buf_.size() < kHeaderSize) return ParseStatus::NeedMore;
        if (std::memcmp(buf_.data(), kMagic, 4) != 0) return ParseStatus::BadMagic;
        if (buf_[4] != kProtocolVersion) return ParseStatus::BadVersion;

        const size_t payloadSize = getU16(&buf_[6]);
        if (payloadSize > kMaxPayload) return ParseStatus::BadLength;

        const size_t total = kHeaderSize + payloadSize;
        if (buf_.size() < total) return ParseStatus::NeedMore;

        out.type = static_cast<MsgType>(buf_[5]);
        out.payload.assign(buf_.begin() + kHeaderSize, buf_.begin() + total);
        buf_.erase(buf_.begin(), buf_.begin() + total);
        return ParseStatus::Ok;
    }

    void reset() { buf_.clear(); }
    size_t buffered() const { return buf_.size(); }

private:
    std::vector<uint8_t> buf_;
};

// ---------------------------------------------------------------------------
// Message builders
// ---------------------------------------------------------------------------

inline std::vector<uint8_t> makeServerHello(uint8_t attemptsLeft,
                                            const std::string& serverName) {
    Writer w(MsgType::ServerHello);
    w.u8(kProtocolVersion);
    w.u8(attemptsLeft);
    w.str(serverName);
    return w.finish();
}

struct ServerHelloInfo {
    uint8_t version = 0;
    uint8_t attemptsLeft = 0;
    std::string name;
};

inline bool parseServerHello(const Frame& frame, ServerHelloInfo& info) {
    Reader r(frame.payload.data(), frame.payload.size());
    return r.u8(info.version) && r.u8(info.attemptsLeft) && r.str(info.name);
}

inline std::vector<uint8_t> makeAuth(uint32_t pin, const std::string& deviceName) {
    Writer w(MsgType::Auth);
    w.u32(pin);
    w.str(deviceName);
    return w.finish();
}

struct AuthInfo {
    uint32_t pin = 0;
    std::string deviceName;
};

inline bool parseAuth(const Frame& frame, AuthInfo& info) {
    Reader r(frame.payload.data(), frame.payload.size());
    return r.u32(info.pin) && r.str(info.deviceName);
}

inline std::vector<uint8_t> makeAuthResult(bool ok, uint8_t attemptsLeft,
                                           const std::string& message) {
    Writer w(MsgType::AuthResult);
    w.u8(ok ? 1 : 0);
    w.u8(attemptsLeft);
    w.str(message);
    return w.finish();
}

struct AuthResultInfo {
    bool ok = false;
    uint8_t attemptsLeft = 0;
    std::string message;
};

inline bool parseAuthResult(const Frame& frame, AuthResultInfo& info) {
    Reader r(frame.payload.data(), frame.payload.size());
    uint8_t ok = 0;
    if (!r.u8(ok) || !r.u8(info.attemptsLeft) || !r.str(info.message)) return false;
    info.ok = (ok != 0);
    return true;
}

inline std::vector<uint8_t> makeMove(int16_t dx, int16_t dy) {
    Writer w(MsgType::Move);
    w.i16(dx);
    w.i16(dy);
    return w.finish();
}

inline bool parseMove(const Frame& frame, int16_t& dx, int16_t& dy) {
    Reader r(frame.payload.data(), frame.payload.size());
    return r.i16(dx) && r.i16(dy);
}

inline std::vector<uint8_t> makeButton(MouseButton button, bool pressed) {
    Writer w(MsgType::Button);
    w.u8(static_cast<uint8_t>(button));
    w.u8(pressed ? 1 : 0);
    return w.finish();
}

struct ButtonInfo {
    uint8_t button = 0;
    bool pressed = false;
};

inline bool parseButton(const Frame& frame, ButtonInfo& info) {
    Reader r(frame.payload.data(), frame.payload.size());
    uint8_t button = 0;
    uint8_t state = 0;
    if (!r.u8(button) || !r.u8(state)) return false;
    if (button < static_cast<uint8_t>(MouseButton::Left) ||
        button > static_cast<uint8_t>(MouseButton::Middle)) {
        return false;
    }
    info.button = button;
    info.pressed = (state != 0);
    return true;
}

// Wheel deltas are expressed in notches (1 notch == WHEEL_DELTA on Windows).
inline std::vector<uint8_t> makeWheel(int16_t dx, int16_t dy) {
    Writer w(MsgType::Wheel);
    w.i16(dx);
    w.i16(dy);
    return w.finish();
}

inline bool parseWheel(const Frame& frame, int16_t& dx, int16_t& dy) {
    Reader r(frame.payload.data(), frame.payload.size());
    return r.i16(dx) && r.i16(dy);
}

// A run of UTF-8 text. The phone sends printable text here; navigation and
// editing keys use the separate Key message so IME events are not duplicated.
inline std::vector<uint8_t> makeText(const std::string& utf8) {
    Writer w(MsgType::Text, utf8.size() + 2);
    w.str(utf8);
    return w.finish();
}

inline bool parseText(const Frame& frame, std::string& utf8) {
    Reader r(frame.payload.data(), frame.payload.size());
    return r.str(utf8) && r.atEnd() && !utf8.empty();
}

inline std::vector<uint8_t> makeKey(KeyCode key) {
    Writer w(MsgType::Key, 1);
    w.u8(static_cast<uint8_t>(key));
    return w.finish();
}

inline bool parseKey(const Frame& frame, KeyCode& key) {
    Reader r(frame.payload.data(), frame.payload.size());
    uint8_t value = 0;
    if (!r.u8(value) || !r.atEnd() ||
        value < static_cast<uint8_t>(KeyCode::Backspace) ||
        value > static_cast<uint8_t>(KeyCode::Right)) {
        return false;
    }
    key = static_cast<KeyCode>(value);
    return true;
}

inline std::vector<uint8_t> makePing(uint32_t token) {
    Writer w(MsgType::Ping);
    w.u32(token);
    return w.finish();
}

inline std::vector<uint8_t> makePong(uint32_t token) {
    Writer w(MsgType::Pong);
    w.u32(token);
    return w.finish();
}

inline bool parseToken(const Frame& frame, uint32_t& token) {
    Reader r(frame.payload.data(), frame.payload.size());
    return r.u32(token);
}

inline std::vector<uint8_t> makeError(ErrorCode code, const std::string& message) {
    Writer w(MsgType::Error);
    w.u8(static_cast<uint8_t>(code));
    w.str(message);
    return w.finish();
}

struct ErrorInfo {
    uint8_t code = 0;
    std::string message;
};

inline bool parseError(const Frame& frame, ErrorInfo& info) {
    Reader r(frame.payload.data(), frame.payload.size());
    return r.u8(info.code) && r.str(info.message);
}

// ---------------------------------------------------------------------------
// Discovery (UDP, unframed)
//
// Request  (client -> broadcast):     'M','C','T','D', version
// Response (server -> client):        'M','C','T','D', version,
//                                     tcpPort (u16), nameLen (u16), name
// ---------------------------------------------------------------------------

inline std::vector<uint8_t> makeDiscoveryRequest() {
    std::vector<uint8_t> out;
    out.push_back('M');
    out.push_back('C');
    out.push_back('T');
    out.push_back('D');
    out.push_back(kProtocolVersion);
    return out;
}

inline bool isDiscoveryRequest(const uint8_t* data, size_t size) {
    return size == 5 && data[0] == 'M' && data[1] == 'C' && data[2] == 'T' &&
           data[3] == 'D' && data[4] == kProtocolVersion;
}

inline std::vector<uint8_t> makeDiscoveryResponse(uint16_t tcpPort,
                                                  const std::string& name) {
    std::vector<uint8_t> out;
    out.push_back('M');
    out.push_back('C');
    out.push_back('T');
    out.push_back('D');
    out.push_back(kProtocolVersion);

    uint8_t tmp[2];
    putU16(tmp, tcpPort);
    out.insert(out.end(), tmp, tmp + 2);

    size_t size = name.size();
    if (size > kMaxStringLength) size = kMaxStringLength;
    putU16(tmp, static_cast<uint16_t>(size));
    out.insert(out.end(), tmp, tmp + 2);
    out.insert(out.end(), name.begin(),
               name.begin() + static_cast<std::ptrdiff_t>(size));
    return out;
}

struct DiscoveryResponse {
    uint16_t tcpPort = 0;
    std::string name;
};

inline bool parseDiscoveryResponse(const uint8_t* data, size_t size,
                                   DiscoveryResponse& out) {
    // magic(4) + version(1) + port(2) + nameLen(2)
    if (size < 9) return false;
    if (std::memcmp(data, "MCTD", 4) != 0) return false;
    if (data[4] != kProtocolVersion) return false;

    out.tcpPort = getU16(data + 5);
    const uint16_t nameSize = getU16(data + 7);
    if (nameSize > kMaxStringLength) return false;
    if (size < 9u + nameSize) return false;

    out.name.assign(reinterpret_cast<const char*>(data + 9), nameSize);
    return true;
}

}  // namespace mctl
