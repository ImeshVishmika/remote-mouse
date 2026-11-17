#pragma once

#include <SDL.h>

#include <cstdint>
#include <map>
#include <vector>

struct PadEvent {
    enum class Kind {
        Move,
        Button,
        Wheel,
    };

    Kind kind = Kind::Move;
    float dx = 0.0f;
    float dy = 0.0f;
    uint8_t button = 1;
    bool pressed = false;
};

// Turns raw finger positions into touchpad actions:
//   * one finger drag        -> cursor movement
//   * tap                    -> left click
//   * long press then drag   -> drag (left button held)
//   * two finger tap         -> right click
//   * two finger drag        -> scroll wheel
//   * three finger tap       -> middle click
class Touchpad {
public:
    void configure(float tapSlopPx, float notchPx, Uint64 longPressMs);

    void begin(SDL_FingerID id, float x, float y, Uint64 nowMs);
    void move(SDL_FingerID id, float x, float y, Uint64 nowMs);
    void end(SDL_FingerID id, float x, float y, Uint64 nowMs);

    // Releases a latched drag button and forgets all touches.
    void cancel();

    std::vector<PadEvent> takeEvents();

private:
    struct Finger {
        float x = 0.0f;
        float y = 0.0f;
        float startX = 0.0f;
        float startY = 0.0f;
    };

    void computeCentroid(float& cx, float& cy) const;
    void baseline();
    void finishDrag();
    void resetGesture(Uint64 nowMs);
    void emitMove(float dx, float dy);
    void emitButton(uint8_t button, bool pressed);
    void emitWheel(float notches);
    void emitClick(uint8_t button);

    std::map<SDL_FingerID, Finger> fingers_;
    std::vector<PadEvent> events_;

    float tapSlopPx_ = 24.0f;
    float notchPx_ = 64.0f;
    Uint64 longPressMs_ = 400;

    Uint64 gestureStartMs_ = 0;
    int maxFingers_ = 0;
    float maxTravel_ = 0.0f;
    float lastX_ = 0.0f;
    float lastY_ = 0.0f;
    bool haveCentroid_ = false;
    bool dragging_ = false;
    bool scrolled_ = false;
    float scrollAccumulator_ = 0.0f;
};
