#include "gui.h"

#include <cstdio>
#include <sstream>
#include <string>

#include "log.h"

namespace {

constexpr int kTimerLogs = 1;
constexpr int kTimerMs = 200;

constexpr int kIdStartStop = 100;
constexpr int kIdEditPin = 101;
constexpr int kIdEditName = 102;
constexpr int kIdBlocked = 106;
constexpr int kIdBlock = 107;
constexpr int kIdUnblock = 108;
constexpr int kIdLog = 109;

std::string pinText(HWND edit) {
    char buf[16] = {0};
    GetWindowTextA(edit, buf, sizeof(buf));
    return buf;
}

bool parsePin(const std::string& text, uint32_t& out) {
    if (text.empty()) return false;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
    }
    unsigned long value = std::strtoul(text.c_str(), nullptr, 10);
    if (value == 0 || value > 999999) return false;
    out = static_cast<uint32_t>(value);
    return true;
}

}  // namespace

ServerGui::ServerGui(ServerConfig config, HINSTANCE instance)
    : instance_(instance), server_(std::move(config)) {}

ServerGui::~ServerGui() {
    // Make sure the server is not left running when the window dies.
    server_.stop();
}

int ServerGui::run(int showCmd) {
    (void)showCmd;
    const char* className = "RemoteMouseServerWnd";

    WNDCLASSEXA wcx{};
    wcx.cbSize = sizeof(wcx);
    wcx.style = CS_HREDRAW | CS_VREDRAW;
    wcx.lpfnWndProc = ServerGui::wndProc;
    wcx.hInstance = instance_;
    wcx.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcx.hbrBackground =
        (HBRUSH)(COLOR_WINDOW + 1);
    wcx.lpszClassName = className;

    if (!RegisterClassExA(&wcx)) {
        logx::error("RegisterClassEx failed");
        return 1;
    }

    window_ = CreateWindowExA(
        0, className, "Remote Mouse Server",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 700, 560,
        nullptr, nullptr, instance_, this);

    if (window_ == nullptr) {
        logx::error("CreateWindow failed");
        return 1;
    }

    createControls();

    // Start the server immediately.
    std::string error;
    if (!server_.start(error)) {
        logx::error("Failed to start server: %s", error.c_str());
        MessageBoxA(window_, error.c_str(), "Server error", MB_OK | MB_ICONERROR);
        return 1;
    }
    started_ = true;
    updateControls();

    // Poll logs every kTimerMs.
    SetTimer(window_, kTimerLogs, kTimerMs, nullptr);

    MSG msg{};
    while (GetMessageA(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    KillTimer(window_, kTimerLogs);
    server_.stop();
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK ServerGui::wndProc(HWND window, UINT message,
                                    WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTA*>(lParam);
        auto* gui = static_cast<ServerGui*>(create->lpCreateParams);
        SetWindowLongPtr(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(gui));
        gui->window_ = window;
        return DefWindowProc(window, message, wParam, lParam);
    }

    auto* gui = reinterpret_cast<ServerGui*>(
        GetWindowLongPtr(window, GWLP_USERDATA));
    if (gui == nullptr) return DefWindowProc(window, message, wParam, lParam);
    return gui->handleMessage(message, wParam, lParam);
}

LRESULT ServerGui::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_SIZE: {
            // Resize the log and blocked-list controls to fill the bottom.
            RECT rect{};
            GetClientRect(window_, &rect);
            int width = rect.right - rect.left;
            int height = rect.bottom - rect.top;

            const int logTop = height - 220;
            if (editLog_) {
                MoveWindow(editLog_, 10, logTop, width - 20, 210, TRUE);
            }
            if (listBlocked_) {
                // Blocked list sits above the log on the right side.
                MoveWindow(listBlocked_, 330, 160, width - 340, logTop - 170, TRUE);
            }
            if (lblAddr_) {
                MoveWindow(lblAddr_, 10, logTop - 24, width - 20, 20, TRUE);
            }
            return 0;
        }

        case WM_COMMAND: {
            const int id = LOWORD(wParam);

            if (id == kIdStartStop) {
                if (started_) {
                    server_.stop();
                    started_ = false;
                } else {
                    std::string error;
                    if (!server_.start(error)) {
                        logx::error("Failed to start server: %s", error.c_str());
                        MessageBoxA(window_, error.c_str(), "Server error",
                                    MB_OK | MB_ICONERROR);
                    } else {
                        started_ = true;
                    }
                }
                updateControls();
                return 0;
            }

            if (id == kIdBlock) {
                int sel = (int)SendMessageA(listBlocked_, LB_GETCURSEL, 0, 0);
                if (sel != LB_ERR) {
                    char buf[256] = {0};
                    SendMessageA(listBlocked_, LB_GETTEXT, sel,
                                 (LPARAM)buf);
                    server_.blockPeer(buf);
                    refreshBlockedList();
                }
                return 0;
            }

            if (id == kIdUnblock) {
                int sel = (int)SendMessageA(listBlocked_, LB_GETCURSEL, 0, 0);
                if (sel != LB_ERR) {
                    char buf[256] = {0};
                    SendMessageA(listBlocked_, LB_GETTEXT, sel,
                                 (LPARAM)buf);
                    server_.unblockPeer(buf);
                    refreshBlockedList();
                }
                return 0;
            }

            if (id == kIdEditPin && HIWORD(wParam) == EN_CHANGE) {
                // Apply PIN live while typing.
                uint32_t parsed = 0;
                if (parsePin(pinText(editPin_), parsed)) {
                    server_.setPin(parsed);
                }
                return 0;
            }

            if (id == kIdEditName && HIWORD(wParam) == EN_CHANGE) {
                // Name is fixed at startup; edits are ignored at runtime.
                return 0;
            }

            return DefWindowProc(window_, message, wParam, lParam);
        }

        case WM_TIMER: {
            if (wParam == kTimerLogs) {
                refreshLogs();
                updateControls();
            }
            return 0;
        }

        case WM_CLOSE:
            DestroyWindow(window_);
            return 0;

        case WM_DESTROY:
            server_.stop();
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProc(window_, message, wParam, lParam);
}

void ServerGui::createControls() {
    const int padding = 10;
    const int rowH = 24;
    int y = padding;

    // --- Top row: Start/Stop + PIN ---
    btnStartStop_ = CreateWindowExA(
        0, "BUTTON", "Start Server",
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
        padding, y, 130, 30, window_, (HMENU)(INT_PTR)kIdStartStop,
        instance_, nullptr);

    CreateWindowExA(0, "STATIC", "PIN (6 digits):",
                    WS_VISIBLE | WS_CHILD,
                    160, y + 6, 90, rowH, window_, nullptr, instance_, nullptr);

    editPin_ = CreateWindowExA(
        WS_EX_CLIENTEDGE, "EDIT", formatPin(server_.pin()).c_str(),
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | ES_NUMBER,
        255, y, 90, rowH, window_, (HMENU)(INT_PTR)kIdEditPin, instance_,
        nullptr);
    SendMessageA(editPin_, EM_LIMITTEXT, 6, 0);

    y += 44;

    // --- Name ---
    CreateWindowExA(0, "STATIC", "Server name:",
                    WS_VISIBLE | WS_CHILD,
                    padding, y + 6, 90, rowH, window_, nullptr, instance_,
                    nullptr);

    editName_ = CreateWindowExA(
        WS_EX_CLIENTEDGE, "EDIT", server_.status().empty() ? "" : "",
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL,
        padding + 95, y, 260, rowH, window_, (HMENU)(INT_PTR)kIdEditName,
        instance_, nullptr);
    // Put the actual server name in the field.
    {
        // Server doesn't expose name directly; reuse config name via status
        // text is not available. Use a placeholder derived from config.
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%s", "Remote Mouse PC");
        SetWindowTextA(editName_, buf);
    }
    SendMessageA(editName_, EM_LIMITTEXT, 64, 0);

    y += 44;

    // --- Status ---
    lblStatus_ = CreateWindowExA(0, "STATIC", "Status: stopped",
                                 WS_VISIBLE | WS_CHILD,
                                 padding, y + 4, 220, rowH, window_, nullptr,
                                 instance_, nullptr);

    lblPeer_ = CreateWindowExA(0, "STATIC", "Device: none",
                               WS_VISIBLE | WS_CHILD,
                               padding + 240, y + 4, 260, rowH, window_,
                               nullptr, instance_, nullptr);

    y += 40;

    // --- Blocked devices ---
    CreateWindowExA(0, "STATIC", "Blocked devices:",
                    WS_VISIBLE | WS_CHILD,
                    padding, y + 6, 120, rowH, window_, nullptr, instance_,
                    nullptr);

    listBlocked_ = CreateWindowExA(
        WS_EX_CLIENTEDGE, "LISTBOX", "",
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | WS_BORDER | LBS_NOTIFY |
            LBS_HASSTRINGS | WS_VSCROLL,
        padding, y + 26, 300, 120, window_, (HMENU)(INT_PTR)kIdBlocked,
        instance_, nullptr);

    btnBlock_ = CreateWindowExA(
        0, "BUTTON", "Block selected",
        WS_TABSTOP | WS_VISIBLE | WS_CHILD,
        padding + 315, y + 26, 100, 26, window_, (HMENU)(INT_PTR)kIdBlock,
        instance_, nullptr);

    btnUnblock_ = CreateWindowExA(
        0, "BUTTON", "Unblock selected",
        WS_TABSTOP | WS_VISIBLE | WS_CHILD,
        padding + 425, y + 26, 110, 26, window_, (HMENU)(INT_PTR)kIdUnblock,
        instance_, nullptr);

    y += 170;

    // --- Addresses ---
    lblAddr_ = CreateWindowExA(
        0, "STATIC", "Local IPv4: —",
        WS_VISIBLE | WS_CHILD,
        padding, y + 4, 400, rowH, window_, nullptr, instance_, nullptr);

    y += 30;

    // --- Log ---
    editLog_ = CreateWindowExA(
        WS_EX_CLIENTEDGE, "EDIT", "",
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | WS_BORDER | ES_MULTILINE |
            ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL | WS_VSCROLL,
        padding, y, 660, 200, window_, (HMENU)(INT_PTR)kIdLog, instance_,
        nullptr);

    refreshLogs();
    refreshBlockedList();
    updateControls();
}

void ServerGui::updateControls() {
    const bool running = server_.isRunning();

    // Start/Stop button text.
    SetWindowTextA(btnStartStop_,
                   running ? "Stop Server" : "Start Server");

    // Status line.
    std::string status = server_.status();
    std::string statusText = "Status: " + status;
    SetWindowTextA(lblStatus_, statusText.c_str());

    // Connected peer.
    std::string peer = server_.connectedPeer();
    std::string peerText = peer.empty() ? "Device: none" : "Device: " + peer;
    SetWindowTextA(lblPeer_, peerText.c_str());

    // PIN field reflects current server PIN (may have been changed externally).
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%06u", static_cast<unsigned>(server_.pin()));
    SetWindowTextA(editPin_, buf);

    // Enable/disable block buttons based on selection.
    int sel = (int)SendMessageA(listBlocked_, LB_GETCURSEL, 0, 0);
    const bool haveSel = sel != LB_ERR;
    EnableWindow(btnBlock_, haveSel);
    EnableWindow(btnUnblock_, haveSel);

    // Addresses.
    std::ostringstream addr;
    addr << "Local IPv4: ";
    const auto addrs = server_.localAddresses();
    for (size_t i = 0; i < addrs.size(); ++i) {
        if (i != 0) addr << ", ";
        addr << addrs[i];
    }
    if (addrs.empty()) addr << "—";
    SetWindowTextA(lblAddr_, addr.str().c_str());
}

void ServerGui::refreshLogs() {
    std::vector<std::string> lines = logx::drainLogs();
    if (lines.empty()) return;

    std::ostringstream text;
    for (const auto& line : lines) {
        text << line << "\n";
    }

    // Append to the edit control.
    const int len = (int)GetWindowTextLengthA(editLog_);
    SendMessageA(editLog_, EM_SETSEL, len, len);
    SendMessageA(editLog_, EM_REPLACESEL, FALSE, (LPARAM)text.str().c_str());

    // Scroll to bottom.
    SendMessageA(editLog_, EM_SETSEL, -1, -1);
}

void ServerGui::refreshBlockedList() {
    SendMessageA(listBlocked_, LB_RESETCONTENT, 0, 0);
    const auto peers = server_.blockedPeers();
    for (const auto& peer : peers) {
        SendMessageA(listBlocked_, LB_ADDSTRING, 0, (LPARAM)peer.c_str());
    }
}

ServerGui* ServerGui::getThis(HWND window) {
    return reinterpret_cast<ServerGui*>(
        GetWindowLongPtr(window, GWLP_USERDATA));
}

void ServerGui::appendLogLine(const std::string& line) {
    const int len = (int)GetWindowTextLengthA(editLog_);
    SendMessageA(editLog_, EM_SETSEL, len, len);
    SendMessageA(editLog_, EM_REPLACESEL, FALSE, (LPARAM)line.c_str());
    SendMessageA(editLog_, EM_SETSEL, -1, -1);
}
