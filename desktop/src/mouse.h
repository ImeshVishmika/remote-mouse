#pragma once

#include <cstdint>

struct MouseStats {
    uint64_t moves = 0;
    uint64_t buttons = 0;
    uint64_t notches = 0;
};

// Windows cursor injection. All coordinates are physical pixels because the
// process is made DPI-aware at startup (see platform.cpp).
class MouseInjector {
public:
    MouseInjector(bool dryRun, float sensitivity);

    void moveRelative(int dx, int dy);
    void setButton(uint8_t button, bool pressed);
    void wheel(int dx, int dy);
    void releaseAll();

    const MouseStats& stats() const { return stats_; }
    void resetStats() { stats_ = MouseStats{}; }

private:
    bool dryRun_;
    float sensitivity_;
    double accumulatorX_ = 0.0;
    double accumulatorY_ = 0.0;
    bool held_[4] = {false, false, false, false};
    MouseStats stats_;
    uint64_t failedMoves_ = 0;
};
