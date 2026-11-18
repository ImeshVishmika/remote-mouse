#include "server_share.h"

#include <SDL.h>

#ifdef __ANDROID__
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <jni.h>
#endif

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace {

constexpr char kDownloadName[] = "remote_mouse_server.exe";
constexpr char kDownloadPath[] = "/remote_mouse_server.exe";
constexpr size_t kMaxRequestSize = 8192;

bool sendAll(int socket, const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    size_t sent = 0;
    while (sent < size) {
        const ssize_t result =
            send(socket, bytes + sent, size - sent, MSG_NOSIGNAL);
        if (result <= 0) return false;
        sent += static_cast<size_t>(result);
    }
    return true;
}

void respond(int client, const char* status, const char* contentType,
             const uint8_t* body, size_t bodySize, bool headOnly,
             const std::string& extraHeaders = std::string()) {
    std::ostringstream headers;
    headers << "HTTP/1.1 " << status << "\r\n"
            << "Content-Type: " << contentType << "\r\n"
            << "Content-Length: " << bodySize << "\r\n"
            << "Cache-Control: no-store\r\n"
            << "X-Content-Type-Options: nosniff\r\n"
            << "Connection: close\r\n"
            << extraHeaders << "\r\n";

    const std::string text = headers.str();
    if (!sendAll(client, text.data(), text.size())) return;
    if (!headOnly && bodySize > 0) {
        sendAll(client, body, bodySize);
    }
}

void respondText(int client, const char* status, const std::string& body,
                 bool headOnly) {
    respond(client, status, "text/plain; charset=utf-8",
            reinterpret_cast<const uint8_t*>(body.data()), body.size(),
            headOnly);
}

std::string makePage(size_t payloadSize) {
    char sizeText[64];
    std::snprintf(sizeText, sizeof(sizeText), "%.2f MB",
                  static_cast<double>(payloadSize) / (1024.0 * 1024.0));

    std::ostringstream page;
    page << R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Remote Mouse PC Server</title>
<style>
:root{color-scheme:dark;--bg:#0b0f14;--card:#111827;--line:#334155;
--text:#e5e7eb;--muted:#94a3b8;--accent:#22d3ee}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);
font:16px/1.6 system-ui,-apple-system,Segoe UI,sans-serif}
main{max-width:680px;margin:0 auto;padding:42px 22px}
.brand{color:var(--accent);font-weight:800;letter-spacing:.16em;font-size:13px}
h1{font-size:32px;line-height:1.2;margin:14px 0}p{color:var(--muted)}
.card{margin-top:26px;padding:24px;border:1px solid var(--line);
border-radius:18px;background:var(--card)}
.download{display:block;background:var(--accent);color:#071116;
text-align:center;text-decoration:none;font-weight:800;padding:16px;
border-radius:12px;letter-spacing:.04em}.file{text-align:center;font-size:13px}
ol{padding-left:22px}li{margin:12px 0}code{color:var(--accent);
overflow-wrap:anywhere}.note{border-left:3px solid var(--accent);
padding-left:14px;margin-top:24px;font-size:14px}
</style>
</head>
<body><main>
<div class="brand">REMOTE MOUSE</div>
<h1>Windows PC server</h1>
<p>This download comes directly from your phone over the local network.
No internet connection is needed.</p>
<div class="card">
<a class="download" href="/remote_mouse_server.exe" download>DOWNLOAD SERVER</a>
<p class="file">remote_mouse_server.exe &middot; )HTML"
         << sizeText << R"HTML(</p>
</div>
<h2>After downloading</h2>
<ol>
<li>Open <b>remote_mouse_server.exe</b> on the Windows PC.</li>
<li>If Windows Firewall asks, allow access on your local network.</li>
<li>Keep the server window open, then return to the phone app.</li>
<li>Your PC should appear automatically. You can also enter its IP manually.</li>
</ol>
<p class="note">For the desktop control window instead of the console, run
<code>remote_mouse_server.exe --gui</code>. If SmartScreen appears, choose
<b>More info</b>, then <b>Run anyway</b>.</p>
</main></body></html>)HTML";
    return page.str();
}

int addressRank(uint32_t address) {
    const uint32_t host = ntohl(address);
    const uint32_t a = (host >> 24) & 0xff;
    const uint32_t b = (host >> 16) & 0xff;
    if (a == 192 && b == 168) return 0;
    if (a == 10) return 1;
    if (a == 172 && b >= 16 && b <= 31) return 2;
    if (a == 169 && b == 254) return 4;
    return 3;
}

std::vector<std::string> localIPv4Addresses() {
    std::vector<std::pair<int, std::string>> ranked;
    const int socket = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socket < 0) return {};

    ifreq interfaces[64];
    ifconf config{};
    config.ifc_len = sizeof(interfaces);
    config.ifc_req = interfaces;

    if (ioctl(socket, SIOCGIFCONF, &config) == 0) {
        const int count = std::min(
            64, static_cast<int>(config.ifc_len / sizeof(ifreq)));
        for (int i = 0; i < count; ++i) {
            ifreq item = interfaces[i];
            if (item.ifr_addr.sa_family != AF_INET) continue;
            if (ioctl(socket, SIOCGIFFLAGS, &item) != 0) continue;
            if ((item.ifr_flags & IFF_UP) == 0 ||
                (item.ifr_flags & IFF_LOOPBACK) != 0) {
                continue;
            }

            const auto* address =
                reinterpret_cast<const sockaddr_in*>(&interfaces[i].ifr_addr);
            char text[INET_ADDRSTRLEN] = {0};
            if (inet_ntop(AF_INET, &address->sin_addr, text, sizeof(text)) !=
                nullptr) {
                ranked.emplace_back(addressRank(address->sin_addr.s_addr),
                                    std::string(text));
            }
        }
    }

    close(socket);
    std::sort(ranked.begin(), ranked.end());

    std::vector<std::string> addresses;
    for (const auto& item : ranked) {
        if (std::find(addresses.begin(), addresses.end(), item.second) ==
            addresses.end()) {
            addresses.push_back(item.second);
        }
    }
    return addresses;
}

}  // namespace

ServerShare::~ServerShare() {
    stop();
}

bool ServerShare::start(uint16_t port) {
    stop();
    stop_ = false;
    port_ = port;
    error_.clear();
    payload_.clear();

    if (!loadPayload()) return false;

    socket_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socket_ < 0) {
        error_ = std::string("HTTP socket failed: ") + std::strerror(errno);
        return false;
    }

    const int enable = 1;
    setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port_);

    if (bind(socket_, reinterpret_cast<const sockaddr*>(&address),
             sizeof(address)) != 0 ||
        listen(socket_, 4) != 0) {
        error_ = std::string("HTTP listen failed: ") + std::strerror(errno);
        close(socket_);
        socket_ = -1;
        return false;
    }

    thread_ = std::thread([this]() { run(); });
    SDL_Log("Remote Mouse: PC server download page on tcp/%u",
            static_cast<unsigned>(port_));
    return true;
}

void ServerShare::stop() {
    stop_ = true;
    if (socket_ >= 0) {
        shutdown(socket_, SHUT_RDWR);
    }
    {
        // Wake a worker blocked in recv/send. The worker closes the descriptor
        // while holding this same mutex, so it cannot be reused accidentally.
        std::lock_guard<std::mutex> lock(clientMutex_);
        if (client_ >= 0) shutdown(client_, SHUT_RDWR);
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    if (socket_ >= 0) {
        close(socket_);
        socket_ = -1;
    }
}

std::vector<std::string> ServerShare::urls() const {
    std::vector<std::string> result;
    if (!running()) return result;
    for (const std::string& address : localIPv4Addresses()) {
        result.push_back("http://" + address + ":" + std::to_string(port_));
    }
    return result;
}

bool ServerShare::loadPayload() {
#ifdef __ANDROID__
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (env == nullptr || activity == nullptr) {
        error_ = "Android asset manager is unavailable";
        return false;
    }

    jclass activityClass = env->GetObjectClass(activity);
    jobject managerObject = nullptr;
    if (activityClass != nullptr) {
        const jmethodID getAssets = env->GetMethodID(
            activityClass, "getAssets", "()Landroid/content/res/AssetManager;");
        if (getAssets != nullptr) {
            managerObject = env->CallObjectMethod(activity, getAssets);
        }
    }

    AAsset* asset = nullptr;
    if (managerObject != nullptr) {
        AAssetManager* manager = AAssetManager_fromJava(env, managerObject);
        if (manager != nullptr) {
            asset = AAssetManager_open(manager, kDownloadName, AASSET_MODE_RANDOM);
        }
    }

    if (asset == nullptr) {
        error_ = "remote_mouse_server.exe is missing from the APK";
    } else {
        const int32_t length = AAsset_getLength(asset);
        if (length > 0) {
            payload_.resize(static_cast<size_t>(length));
            size_t offset = 0;
            while (offset < payload_.size()) {
                const int count = AAsset_read(asset, payload_.data() + offset,
                                              payload_.size() - offset);
                if (count <= 0) break;
                offset += static_cast<size_t>(count);
            }
            if (offset != payload_.size()) {
                payload_.clear();
                error_ = "Could not read the bundled Windows server";
            }
        } else {
            error_ = "The bundled Windows server is empty";
        }
        AAsset_close(asset);
    }

    if (managerObject != nullptr) env->DeleteLocalRef(managerObject);
    if (activityClass != nullptr) env->DeleteLocalRef(activityClass);
    env->DeleteLocalRef(activity);

    if (!error_.empty()) SDL_Log("Remote Mouse: %s", error_.c_str());
    return error_.empty();
#else
    error_ = "Phone downloads are only available on Android";
    return false;
#endif
}

void ServerShare::run() {
    const int listener = socket_;
    while (!stop_.load()) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(listener, &readSet);

        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 500 * 1000;

        const int ready =
            select(listener + 1, &readSet, nullptr, nullptr, &timeout);
        if (ready <= 0) continue;

        const int client = accept(listener, nullptr, nullptr);
        if (client < 0) continue;
        {
            std::lock_guard<std::mutex> lock(clientMutex_);
            if (stop_.load()) {
                close(client);
                break;
            }
            client_ = client;
        }

        serve(client);

        {
            std::lock_guard<std::mutex> lock(clientMutex_);
            client_ = -1;
            close(client);
        }
    }
}

void ServerShare::serve(int client) {
    timeval timeout{};
    timeout.tv_sec = 3;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    std::string request;
    char buffer[1024];
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(10);
    while (request.find("\r\n\r\n") == std::string::npos &&
           request.size() < kMaxRequestSize && !stop_.load() &&
           std::chrono::steady_clock::now() < deadline) {
        const ssize_t count = recv(client, buffer, sizeof(buffer), 0);
        if (count <= 0) return;
        request.append(buffer, static_cast<size_t>(count));
    }

    if (stop_.load()) return;
    if (request.find("\r\n\r\n") == std::string::npos) {
        respondText(client, "400 Bad Request", "Bad request\n", false);
        return;
    }

    const size_t lineEnd = request.find("\r\n");
    if (lineEnd == std::string::npos) return;

    std::istringstream line(request.substr(0, lineEnd));
    std::string method;
    std::string target;
    line >> method >> target;

    const bool headOnly = method == "HEAD";
    if (method != "GET" && !headOnly) {
        respondText(client, "405 Method Not Allowed", "Method not allowed\n",
                    false);
        return;
    }

    const size_t query = target.find('?');
    if (query != std::string::npos) target.resize(query);

    if (target == "/" || target == "/index.html") {
        const std::string page = makePage(payload_.size());
        respond(client, "200 OK", "text/html; charset=utf-8",
                reinterpret_cast<const uint8_t*>(page.data()), page.size(),
                headOnly);
        return;
    }

    if (target == kDownloadPath) {
        respond(client, "200 OK", "application/octet-stream", payload_.data(),
                payload_.size(), headOnly,
                std::string("Content-Disposition: attachment; filename=\"") +
                    kDownloadName + "\"\r\n");
        return;
    }

    respondText(client, "404 Not Found", "Not found\n", headOnly);
}
