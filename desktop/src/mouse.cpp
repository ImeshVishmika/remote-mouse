#include "mouse.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "log.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

#ifdef _WIN32
constexpr DWORD kButtonFlags[4][2] = {
    {0, 0},
    {MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP},
    {MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP},
    {MOUSEEVENTF_MIDDLEDOWN, MOUSEEVENTF_MIDDLEUP},
};
#endif

const char* buttonName(uint8_t button) {
    switch (button) {
        case 1: return "left";
        case 2: return "right";
        case 3: return "middle";
        default: return "?";
    }
}

}  // namespace

MouseInjector::MouseInjector(bool dryRun, float sensitivity)
    : dryRun_(dryRun), sensitivity_(sensitivity) {}

void MouseInjector::moveRelative(int dx, int dy) {
    if (dx == 0 && dy == 0) return;

    stats_.moves++;
    if (dryRun_) return;

#ifdef _WIN32
    accumulatorX_ += static_cast<double>(dx) * static_cast<double>(sensitivity_);
    accumulatorY_ += static_cast<double>(dy) * static_cast<double>(sensitivity_);

    const int stepX = static_cast<int>(std::trunc(accumulatorX_));
    const int stepY = static_cast<int>(std::trunc(accumulatorY_));
    if (stepX == 0 && stepY == 0) return;

    accumulatorX_ -= static_cast<double>(stepX);
    accumulatorY_ -= static_cast<double>(stepY);

    POINT cursor{};
    if (!GetCursorPos(&cursor)) {
        failedMoves_++;
        if (failedMoves_ <= 3 || failedMoves_ % 500 == 0) {
            logx::warn("GetCursorPos failed (%lu); moves lost: %llu",
                       static_cast<unsigned long>(GetLastError()),
                       static_cast<unsigned long long>(failedMoves_));
        }
        return;
    }

    SetCursorPos(cursor.x + stepX, cursor.y + stepY);
#endif
}

void MouseInjector::setButton(uint8_t button, bool pressed) {
    if (button < 1 || button > 3) return;

    const bool wasHeld = held_[button];
    if (wasHeld == pressed) return;  // ignore duplicates, keeps stats honest

    held_[button] = pressed;
    stats_.buttons++;

    if (dryRun_) {
        logx::event("dry-run: %s button %s", pressed ? "press" : "release",
                    buttonName(button));
        return;
    }

#ifdef _WIN32
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = kButtonFlags[button][pressed ? 0 : 1];
    if (SendInput(1, &input, sizeof(INPUT)) != 1) {
        logx::warn("SendInput failed for %s button (%lu)", buttonName(button),
                   static_cast<unsigned long>(GetLastError()));
    }
#endif
}

void MouseInjector::wheel(int dx, int dy) {
    if (dx == 0 && dy == 0) return;

    const int clampedX = std::max(-30, std::min(30, dx));
    const int clampedY = std::max(-30, std::min(30, dy));
    stats_.notches += static_cast<uint64_t>(std::abs(clampedX) + std::abs(clampedY));

    if (dryRun_) {
        logx::event("dry-run: wheel x=%d y=%d", clampedX, clampedY);
        return;
    }

#ifdef _WIN32
    INPUT inputs[2]{};
    UINT count = 0;

    if (clampedY != 0) {
        inputs[count].type = INPUT_MOUSE;
        inputs[count].mi.dwFlags = MOUSEEVENTF_WHEEL;
        inputs[count].mi.mouseData =
            static_cast<DWORD>(static_cast<int32_t>(clampedY * WHEEL_DELTA));
        count++;
    }
    if (clampedX != 0) {
        inputs[count].type = INPUT_MOUSE;
        inputs[count].mi.dwFlags = MOUSEEVENTF_HWHEEL;
        inputs[count].mi.mouseData =
            static_cast<DWORD>(static_cast<int32_t>(clampedX * WHEEL_DELTA));
        count++;
    }

    if (count != 0 && SendInput(count, inputs, sizeof(INPUT)) != count) {
        logx::warn("SendInput failed for wheel (%lu)",
                   static_cast<unsigned long>(GetLastError()));
    }
#endif
}

void MouseInjector::releaseAll() {
    for (uint8_t button = 1; button <= 3; ++button) {
        if (held_[button]) setButton(button, false);
    }
}
