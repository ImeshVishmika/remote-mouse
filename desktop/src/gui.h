#pragma once

#include <windows.h>

#include "server.h"

// Native Win32 desktop UI for the Remote Mouse server.
// Runs on the main thread; the Server runs on its own background thread and is
// driven via start()/stop(). Log lines are polled with a timer.
class ServerGui {
public:
    ServerGui(ServerConfig config, HINSTANCE instance);
    ~ServerGui();

    // Creates the window, starts the server in the background, then enters the
    // message loop. Returns the exit code.
    int run(int showCmd);

private:
    static LRESULT CALLBACK wndProc(HWND window, UINT message,
                                    WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void createControls();
    void updateControls();
    void refreshLogs();
    void refreshBlockedList();
    void appendLogLine(const std::string& line);

    static ServerGui* getThis(HWND window);

    HINSTANCE instance_;
    HWND window_ = nullptr;

    Server server_;
    bool started_ = false;

    // Control handles
    HWND btnStartStop_ = nullptr;
    HWND editPin_ = nullptr;
    HWND editName_ = nullptr;
    HWND lblStatus_ = nullptr;
    HWND lblPeer_ = nullptr;
    HWND lblAddr_ = nullptr;
    HWND listBlocked_ = nullptr;
    HWND btnBlock_ = nullptr;
    HWND btnUnblock_ = nullptr;
    HWND editLog_ = nullptr;
};
