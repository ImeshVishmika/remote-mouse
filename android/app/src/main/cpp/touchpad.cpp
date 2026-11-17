#include "touchpad.h"

#include <algorithm>
#include <cmath>

void Touchpad::configure(float tapSlopPx, float notchPx, Uint64 longPressMs) {
    tapSlopPx_ = std::max(1.0f, tapSlopPx);
    notchPx_ = std::max(1.0f, notchPx);
    longPressMs_ = longPressMs;
}

void Touchpad::resetGesture(Uint64 nowMs) {
    gestureStartMs_ = nowMs;
    maxFingers_ = 0;
    maxTravel_ = 0.0f;
    dragging_ = false;
    scrolled_ = false;
    scrollAccumulator_ = 0.0f;
    haveCentroid_ = false;
}

void Touchpad::begin(SDL_FingerID id, float x, float y, Uint64 nowMs) {
    if (fingers_.empty()) resetGesture(nowMs);

    Finger finger;
    finger.x = x;
    finger.y = y;
    finger.startX = x;
    finger.startY = y;
    fingers_[id] = finger;

    maxFingers_ = std::max(maxFingers_, static_cast<int>(fingers_.size()));

    if (fingers_.size() >= 2) {
        // A second finger always ends a latched drag.
        finishDrag();
    }

    baseline();
}

void Touchpad::move(SDL_FingerID id, float x, float y, Uint64 nowMs) {
    auto it = fingers_.find(id);
    if (it == fingers_.end()) return;

    it->second.x = x;
    it->second.y = y;

    const float travel = std::hypot(x - it->second.startX, y - it->second.startY);
    maxTravel_ = std::max(maxTravel_, travel);

    float cx = 0.0f;
    float cy = 0.0f;
    computeCentroid(cx, cy);

    if (!haveCentroid_) {
        lastX_ = cx;
        lastY_ = cy;
        haveCentroid_ = true;
        return;
    }

    const float dx = cx - lastX_;
    const float dy = cy - lastY_;
    lastX_ = cx;
    lastY_ = cy;

    if (fingers_.size() == 1) {
        if (!dragging_ && (nowMs - gestureStartMs_) >= longPressMs_ &&
            maxTravel_ <= tapSlopPx_) {
            dragging_ = true;
            emitButton(1, true);
        }
        if (std::fabs(dx) > 0.01f || std::fabs(dy) > 0.01f) {
            emitMove(dx, dy);
        }
        return;
    }

    // Two or more fingers: scroll from the vertical centroid movement.
    scrollAccumulator_ += dy;
    const int notches =
        static_cast<int>(std::trunc(scrollAccumulator_ / notchPx_));
    if (notches != 0) {
        scrollAccumulator_ -= static_cast<float>(notches) * notchPx_;
        scrolled_ = true;
        emitWheel(static_cast<float>(notches));
    }
}

void Touchpad::end(SDL_FingerID id, float x, float y, Uint64 nowMs) {
    auto it = fingers_.find(id);
    if (it == fingers_.end()) return;

    it->second.x = x;
    it->second.y = y;
    maxTravel_ = std::max(
        maxTravel_, std::hypot(x - it->second.startX, y - it->second.startY));

    fingers_.erase(it);
    const Uint64 duration = (nowMs >= gestureStartMs_) ? (nowMs - gestureStartMs_) : 0;

    if (!fingers_.empty()) {
        baseline();
        return;
    }

    if (dragging_) {
        finishDrag();
    } else if (!scrolled_) {
        const float relaxedSlop = tapSlopPx_ * 1.5f;
        if (maxFingers_ == 1 && duration <= 300 && maxTravel_ <= tapSlopPx_) {
            emitClick(1);
        } else if (maxFingers_ == 2 && duration <= 400 &&
                   maxTravel_ <= relaxedSlop) {
            emitClick(2);
        } else if (maxFingers_ == 3 && duration <= 500 &&
                   maxTravel_ <= relaxedSlop) {
            emitClick(3);
        }
    }

    resetGesture(nowMs);
}

void Touchpad::cancel() {
    finishDrag();
    fingers_.clear();
    haveCentroid_ = false;
    scrolled_ = false;
    scrollAccumulator_ = 0.0f;
    maxFingers_ = 0;
    maxTravel_ = 0.0f;
}

std::vector<PadEvent> Touchpad::takeEvents() {
    std::vector<PadEvent> events;
    events.swap(events_);
    return events;
}

void Touchpad::computeCentroid(float& cx, float& cy) const {
    if (fingers_.empty()) {
        cx = 0.0f;
        cy = 0.0f;
        return;
    }

    float sumX = 0.0f;
    float sumY = 0.0f;
    for (const auto& entry : fingers_) {
        sumX += entry.second.x;
        sumY += entry.second.y;
    }
    const float count = static_cast<float>(fingers_.size());
    cx = sumX / count;
    cy = sumY / count;
}

void Touchpad::baseline() {
    if (fingers_.empty()) {
        haveCentroid_ = false;
        return;
    }
    computeCentroid(lastX_, lastY_);
    haveCentroid_ = true;
}

void Touchpad::finishDrag() {
    if (!dragging_) return;
    dragging_ = false;
    emitButton(1, false);
}

void Touchpad::emitMove(float dx, float dy) {
    PadEvent event;
    event.kind = PadEvent::Kind::Move;
    event.dx = dx;
    event.dy = dy;
    events_.push_back(event);
}

void Touchpad::emitButton(uint8_t button, bool pressed) {
    PadEvent event;
    event.kind = PadEvent::Kind::Button;
    event.button = button;
    event.pressed = pressed;
    events_.push_back(event);
}

void Touchpad::emitWheel(float notches) {
    PadEvent event;
    event.kind = PadEvent::Kind::Wheel;
    event.dy = notches;
    events_.push_back(event);
}

void Touchpad::emitClick(uint8_t button) {
    emitButton(button, true);
    emitButton(button, false);
}
