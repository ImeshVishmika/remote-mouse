#include "platform.h"

#include <algorithm>
#include <atomic>
#include <cstdint>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>

#include <iphlpapi.h>

#ifdef _MSC_VER
#pragma comment(lib, "iphlpapi.lib")
#endif
#endif

namespace {

std::atomic<bool>& quitFlag() {
    static std::atomic<bool> flag{false};
    return flag;
}

#ifdef _WIN32
BOOL WINAPI consoleHandler(DWORD event) {
    switch (event) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
            quitFlag().store(true);
            return TRUE;
        default:
            // Let Windows handle close/logoff/shutdown normally.
            return FALSE;
    }
}
#endif

}  // namespace

void platformInit() {
#ifdef _WIN32
    // Physical pixel coordinates keep cursor movement 1:1 with the phone.
    SetProcessDPIAware();
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

bool platformInstallQuitHandler() {
#ifdef _WIN32
    return SetConsoleCtrlHandler(consoleHandler, TRUE) != 0;
#else
    return true;
#endif
}

bool platformQuitRequested() {
    return quitFlag().load();
}

std::vector<std::string> platformLocalIpv4() {
    std::vector<std::string> addresses;

#ifdef _WIN32
    const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                        GAA_FLAG_SKIP_DNS_SERVER;

    ULONG size = 16 * 1024;
    std::vector<uint8_t> buffer(size);

    ULONG status = GetAdaptersAddresses(
        AF_INET, flags, nullptr,
        reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &size);
    if (status == ERROR_BUFFER_OVERFLOW) {
        buffer.resize(size);
        status = GetAdaptersAddresses(
            AF_INET, flags, nullptr,
            reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &size);
    }
    if (status != NO_ERROR) return addresses;

    for (const IP_ADAPTER_ADDRESSES* adapter =
             reinterpret_cast<const IP_ADAPTER_ADDRESSES*>(buffer.data());
         adapter != nullptr; adapter = adapter->Next) {
        if (adapter->OperStatus != IfOperStatusUp) continue;
        if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;

        for (const IP_ADAPTER_UNICAST_ADDRESS* unicast =
                 adapter->FirstUnicastAddress;
             unicast != nullptr; unicast = unicast->Next) {
            const sockaddr* address = unicast->Address.lpSockaddr;
            if (address == nullptr || address->sa_family != AF_INET) continue;

            char text[INET_ADDRSTRLEN] = {0};
            const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(address);
            if (inet_ntop(AF_INET, &ipv4->sin_addr, text, sizeof(text)) ==
                nullptr) {
                continue;
            }

            const std::string value(text);
            if (value == "0.0.0.0") continue;
            // 169.254.x.x is the APIPA fallback when DHCP failed; not useful
            // for typing into the phone.
            if (value.rfind("169.254.", 0) == 0) continue;
            if (std::find(addresses.begin(), addresses.end(), value) ==
                addresses.end()) {
                addresses.push_back(value);
            }
        }
    }
#endif

    return addresses;
}
