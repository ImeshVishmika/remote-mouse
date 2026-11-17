#pragma once

#include <string>
#include <vector>

// Small helpers for process-level Windows setup. No-op on other platforms.

void platformInit();

// Installs a console control handler so Ctrl+C/Ctrl+Break request a clean
// shutdown instead of killing the process mid-session.
bool platformInstallQuitHandler();

// True once Ctrl+C (or another handled control event) was received.
bool platformQuitRequested();

// Non-loopback IPv4 addresses of this machine, so the user can type one of them
// into the phone when UDP discovery is blocked. Empty when unavailable.
std::vector<std::string> platformLocalIpv4();
