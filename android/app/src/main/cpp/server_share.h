#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

constexpr uint16_t kServerSharePort = 47825;

// Serves the Windows server bundled in the APK over the local network.
// No internet connection is involved: a PC on the same Wi-Fi or phone hotspot
// downloads http://<phone-ip>:47825/remote_mouse_server.exe directly.
class ServerShare {
public:
    ~ServerShare();

    bool start(uint16_t port = kServerSharePort);
    void stop();

    bool running() const { return socket_ >= 0; }
    bool available() const { return !payload_.empty(); }
    size_t payloadSize() const { return payload_.size(); }
    const std::string& error() const { return error_; }

    std::vector<std::string> urls() const;

private:
    bool loadPayload();
    void run();
    void serve(int client);

    int socket_ = -1;
    uint16_t port_ = kServerSharePort;
    std::atomic<bool> stop_{false};
    std::thread thread_;
    std::mutex clientMutex_;
    int client_ = -1;
    std::vector<uint8_t> payload_;
    std::string error_;
};
