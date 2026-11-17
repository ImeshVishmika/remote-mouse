#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "protocol.h"

namespace {

int g_failures = 0;

#define CHECK(expression)                                                    \
    do {                                                                     \
        if (!(expression)) {                                                 \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,     \
                         #expression);                                       \
            ++g_failures;                                                    \
        }                                                                    \
    } while (false)

std::vector<uint8_t> rawFrame(uint8_t version, uint8_t type, uint16_t length) {
    std::vector<uint8_t> frame;
    frame.insert(frame.end(), mctl::kMagic, mctl::kMagic + 4);
    frame.push_back(version);
    frame.push_back(type);
    uint8_t size[2];
    mctl::putU16(size, length);
    frame.insert(frame.end(), size, size + 2);
    return frame;
}

void testMoveGoldenBytes() {
    const std::vector<uint8_t> frame = mctl::makeMove(-5, 300);

    CHECK(frame.size() == mctl::kHeaderSize + 4);
    CHECK(frame[0] == 'M');
    CHECK(frame[1] == 'C');
    CHECK(frame[2] == 'T');
    CHECK(frame[3] == 'L');
    CHECK(frame[4] == mctl::kProtocolVersion);
    CHECK(frame[5] == static_cast<uint8_t>(mctl::MsgType::Move));
    CHECK(frame[6] == 4);
    CHECK(frame[7] == 0);
    // -5 as little-endian int16
    CHECK(frame[8] == 0xFB);
    CHECK(frame[9] == 0xFF);
    // 300 as little-endian int16
    CHECK(frame[10] == 0x2C);
    CHECK(frame[11] == 0x01);
}

void testRoundTrips() {
    {
        const std::vector<uint8_t> frame = mctl::makeServerHello(2, "desk-pc");
        mctl::FrameParser parser;
        parser.push(frame.data(), frame.size());
        mctl::Frame out;
        CHECK(parser.next(out) == mctl::ParseStatus::Ok);
        CHECK(out.type == mctl::MsgType::ServerHello);

        mctl::ServerHelloInfo info;
        CHECK(mctl::parseServerHello(out, info));
        CHECK(info.version == mctl::kProtocolVersion);
        CHECK(info.attemptsLeft == 2);
        CHECK(info.name == "desk-pc");
        CHECK(parser.next(out) == mctl::ParseStatus::NeedMore);
    }
    {
        const std::vector<uint8_t> frame = mctl::makeAuth(123456, "Pixel 8");
        mctl::FrameParser parser;
        parser.push(frame.data(), frame.size());
        mctl::Frame out;
        CHECK(parser.next(out) == mctl::ParseStatus::Ok);

        mctl::AuthInfo info;
        CHECK(mctl::parseAuth(out, info));
        CHECK(info.pin == 123456u);
        CHECK(info.deviceName == "Pixel 8");
    }
    {
        const std::vector<uint8_t> frame = mctl::makeAuthResult(false, 1, "Wrong PIN");
        mctl::FrameParser parser;
        parser.push(frame.data(), frame.size());
        mctl::Frame out;
        CHECK(parser.next(out) == mctl::ParseStatus::Ok);

        mctl::AuthResultInfo info;
        CHECK(mctl::parseAuthResult(out, info));
        CHECK(!info.ok);
        CHECK(info.attemptsLeft == 1);
        CHECK(info.message == "Wrong PIN");
    }
    {
        const std::vector<uint8_t> frame =
            mctl::makeButton(mctl::MouseButton::Right, true);
        mctl::FrameParser parser;
        parser.push(frame.data(), frame.size());
        mctl::Frame out;
        CHECK(parser.next(out) == mctl::ParseStatus::Ok);
        CHECK(out.type == mctl::MsgType::Button);

        mctl::ButtonInfo info;
        CHECK(mctl::parseButton(out, info));
        CHECK(info.button == static_cast<uint8_t>(mctl::MouseButton::Right));
        CHECK(info.pressed);
    }
    {
        const std::vector<uint8_t> frame = mctl::makeWheel(-2, 3);
        mctl::FrameParser parser;
        parser.push(frame.data(), frame.size());
        mctl::Frame out;
        CHECK(parser.next(out) == mctl::ParseStatus::Ok);

        int16_t dx = 0;
        int16_t dy = 0;
        CHECK(mctl::parseWheel(out, dx, dy));
        CHECK(dx == -2);
        CHECK(dy == 3);
    }
    {
        const std::vector<uint8_t> frame = mctl::makePing(0xA1B2C3D4u);
        mctl::FrameParser parser;
        parser.push(frame.data(), frame.size());
        mctl::Frame out;
        CHECK(parser.next(out) == mctl::ParseStatus::Ok);
        CHECK(out.type == mctl::MsgType::Ping);

        uint32_t token = 0;
        CHECK(mctl::parseToken(out, token));
        CHECK(token == 0xA1B2C3D4u);
    }
    {
        const std::vector<uint8_t> frame =
            mctl::makeError(mctl::ErrorCode::Busy, "already connected");
        mctl::FrameParser parser;
        parser.push(frame.data(), frame.size());
        mctl::Frame out;
        CHECK(parser.next(out) == mctl::ParseStatus::Ok);
        CHECK(out.type == mctl::MsgType::Error);

        mctl::ErrorInfo info;
        CHECK(mctl::parseError(out, info));
        CHECK(info.code == static_cast<uint8_t>(mctl::ErrorCode::Busy));
        CHECK(info.message == "already connected");
    }
}

void testParserIncremental() {
    const std::vector<uint8_t> first = mctl::makeMove(10, -10);
    const std::vector<uint8_t> second = mctl::makePing(7);

    mctl::FrameParser parser;
    mctl::Frame out;

    // Feed the header byte by byte; nothing complete until the last one.
    for (size_t i = 0; i + 1 < mctl::kHeaderSize; ++i) {
        parser.push(&first[i], 1);
        CHECK(parser.next(out) == mctl::ParseStatus::NeedMore);
    }
    parser.push(&first[mctl::kHeaderSize - 1], first.size() - (mctl::kHeaderSize - 1));
    CHECK(parser.next(out) == mctl::ParseStatus::Ok);
    CHECK(out.type == mctl::MsgType::Move);

    // Two frames in one buffer.
    parser.push(second.data(), second.size());
    CHECK(parser.next(out) == mctl::ParseStatus::Ok);
    CHECK(out.type == mctl::MsgType::Ping);
    CHECK(parser.buffered() == 0);
}

void testParserRejectsBadInput() {
    mctl::FrameParser parser;
    mctl::Frame out;

    std::vector<uint8_t> badMagic = rawFrame(mctl::kProtocolVersion,
                                            static_cast<uint8_t>(mctl::MsgType::Move), 4);
    badMagic[0] = 'X';
    parser.push(badMagic.data(), badMagic.size());
    CHECK(parser.next(out) == mctl::ParseStatus::BadMagic);

    parser.reset();
    std::vector<uint8_t> badVersion =
        rawFrame(99, static_cast<uint8_t>(mctl::MsgType::Move), 4);
    parser.push(badVersion.data(), badVersion.size());
    CHECK(parser.next(out) == mctl::ParseStatus::BadVersion);

    parser.reset();
    std::vector<uint8_t> badLength =
        rawFrame(mctl::kProtocolVersion, static_cast<uint8_t>(mctl::MsgType::Move),
                 static_cast<uint16_t>(mctl::kMaxPayload + 1));
    parser.push(badLength.data(), badLength.size());
    CHECK(parser.next(out) == mctl::ParseStatus::BadLength);

    // Header claims 4 payload bytes, only 3 arrived.
    parser.reset();
    std::vector<uint8_t> truncated =
        rawFrame(mctl::kProtocolVersion, static_cast<uint8_t>(mctl::MsgType::Move), 4);
    const uint8_t partial[3] = {1, 2, 3};
    truncated.insert(truncated.end(), partial, partial + 3);
    parser.push(truncated.data(), truncated.size());
    CHECK(parser.next(out) == mctl::ParseStatus::NeedMore);
}

void testTruncatedPayloads() {
    mctl::Frame frame;
    frame.type = mctl::MsgType::Move;
    frame.payload = {0x01, 0x02};

    int16_t dx = 0;
    int16_t dy = 0;
    CHECK(!mctl::parseMove(frame, dx, dy));

    mctl::Frame auth;
    auth.type = mctl::MsgType::Auth;
    auth.payload = {0x01};
    mctl::AuthInfo info;
    CHECK(!mctl::parseAuth(auth, info));

    mctl::Frame button;
    button.type = mctl::MsgType::Button;
    button.payload = {9, 1};
    mctl::ButtonInfo buttonInfo;
    CHECK(!mctl::parseButton(button, buttonInfo));

    button.payload = {static_cast<uint8_t>(mctl::MouseButton::Left), 1};
    CHECK(mctl::parseButton(button, buttonInfo));
    CHECK(buttonInfo.pressed);

    // Declared string length larger than the remaining payload.
    mctl::Frame oversized;
    oversized.type = mctl::MsgType::Auth;
    oversized.payload.resize(4 + 2 + 2);
    mctl::putU32(oversized.payload.data(), 42);
    mctl::putU16(oversized.payload.data() + 4, 255);
    mctl::AuthInfo oversizedInfo;
    CHECK(!mctl::parseAuth(oversized, oversizedInfo));
}

void testStringTruncation() {
    const std::string longName(400, 'a');
    const std::vector<uint8_t> frame = mctl::makeAuth(1, longName);

    mctl::FrameParser parser;
    parser.push(frame.data(), frame.size());
    mctl::Frame out;
    CHECK(parser.next(out) == mctl::ParseStatus::Ok);

    mctl::AuthInfo info;
    CHECK(mctl::parseAuth(out, info));
    CHECK(info.deviceName.size() == mctl::kMaxStringLength);
}

void testDiscovery() {
    const std::vector<uint8_t> request = mctl::makeDiscoveryRequest();
    CHECK(request.size() == 5);
    CHECK(request[0] == 'M');
    CHECK(request[4] == mctl::kProtocolVersion);
    CHECK(mctl::isDiscoveryRequest(request.data(), request.size()));
    CHECK(!mctl::isDiscoveryRequest(request.data(), 4));

    const std::vector<uint8_t> response =
        mctl::makeDiscoveryResponse(47824, "pc-1");
    CHECK(response.size() == 9 + 4);
    CHECK(response[0] == 'M');
    CHECK(response[4] == mctl::kProtocolVersion);
    // 47824 == 0xBAD0
    CHECK(response[5] == 0xD0);
    CHECK(response[6] == 0xBA);
    CHECK(response[7] == 4);
    CHECK(response[8] == 0);
    CHECK(response[9] == 'p');
    CHECK(response[12] == '1');

    mctl::DiscoveryResponse info;
    CHECK(mctl::parseDiscoveryResponse(response.data(), response.size(), info));
    CHECK(info.tcpPort == 47824);
    CHECK(info.name == "pc-1");

    CHECK(!mctl::parseDiscoveryResponse(response.data(), response.size() - 1, info));

    // Wrong magic.
    std::vector<uint8_t> wrong = response;
    wrong[1] = 'X';
    CHECK(!mctl::parseDiscoveryResponse(wrong.data(), wrong.size(), info));

    // Unknown version.
    std::vector<uint8_t> version = response;
    version[4] = 99;
    CHECK(!mctl::parseDiscoveryResponse(version.data(), version.size(), info));
}

void testClamping() {
    CHECK(mctl::clampToI16(0) == 0);
    CHECK(mctl::clampToI16(40000) == 32767);
    CHECK(mctl::clampToI16(-40000) == -32768);
    CHECK(mctl::clampToI16(1234) == 1234);
}

}  // namespace

int main() {
    testMoveGoldenBytes();
    testRoundTrips();
    testParserIncremental();
    testParserRejectsBadInput();
    testTruncatedPayloads();
    testStringTruncation();
    testDiscovery();
    testClamping();

    if (g_failures != 0) {
        std::fprintf(stderr, "%d protocol test(s) failed\n", g_failures);
        return 1;
    }
    std::printf("protocol tests: all checks passed\n");
    return 0;
}
