#include <SDL.h>

#ifdef __ANDROID__
#include <jni.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "discovery.h"
#include "net.h"
#include "protocol.h"
#include "touchpad.h"
#include "ui.h"

namespace {

constexpr Sint64 kMousePointerId = 0x4D4F5553;  // 'MOUS'
constexpr float kMinSensitivity = 0.6f;
constexpr float kMaxSensitivity = 3.0f;
constexpr float kSensitivityStep = 0.1f;

uint64_t nowMs() {
    return static_cast<uint64_t>(SDL_GetTicks64());
}

Rect inset(const Rect& rect, float amount) {
    return Rect{rect.x + amount, rect.y + amount,
                std::max(0.0f, rect.w - amount * 2.0f),
                std::max(0.0f, rect.h - amount * 2.0f)};
}

#ifdef __ANDROID__
// Ask the Android Activity to change its requested orientation.
// Values match android.content.pm.ActivityInfo:
//   0 = SENSOR, 1 = PORTRAIT, 2 = LANDSCAPE
void setScreenOrientation(int orientation) {
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    if (env == nullptr) return;

    jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (activity == nullptr) return;

    jclass cls = env->GetObjectClass(activity);
    if (cls == nullptr) {
        env->DeleteLocalRef(activity);
        return;
    }

    jmethodID mid =
        env->GetMethodID(cls, "setRequestedOrientation", "(I)V");
    if (mid != nullptr) {
        env->CallVoidMethod(activity, mid, orientation);
    }

    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
}
#endif  // __ANDROID__

std::string fitText(const std::string& value, float maxWidth, int scale) {
    if (maxWidth <= 0.0f) return std::string();
    if (ui::textWidth(value, scale) <= maxWidth) return value;
    if (ui::textWidth("...", scale) > maxWidth) return std::string();

    std::string out;
    for (const char c : value) {
        const std::string candidate = out + c + "...";
        if (ui::textWidth(candidate, scale) > maxWidth) break;
        out.push_back(c);
    }
    return out + "...";
}

enum class Action {
    None,
    PickServer,
    OpenManual,
    ManualDigit,
    ManualDot,
    ManualBackspace,
    ManualConnect,
    PinDigit,
    PinBackspace,
    Disconnect,
    SensitivityDown,
    SensitivityUp,
    GoBack,
    Rotate,
};

enum class RotationMode {
    Auto,
    Portrait,
    Landscape,
};

enum class Look {
    Card,
    Primary,
    Secondary,
    Ghost,
    Danger,
};

struct Button {
    Rect rect;
    Action action = Action::None;
    int value = 0;
    std::string label;
    std::string sub;
    std::string data;
    int textScale = 2;
    Look look = Look::Secondary;
    bool enabled = true;
};

enum class Screen {
    Discover,
    Manual,
    Pin,
    Pad,
};

class App {
public:
    int run();

private:
    bool init();
    void shutdown();
    void frame();
    void handleEvent(const SDL_Event& event);

    void updateLayout();
    void layoutDiscover();
    void layoutManual();
    void layoutPin();
    void layoutPad();
    void layoutKeypad(float top, float bottom, Action digitAction,
                      Action backAction, bool includeDot);

    void applyRotationMode();
    std::string rotationLabel() const;

    void render();
    void renderDiscover();
    void renderManual();
    void renderPin();
    void renderPad();
    void drawButtons();

    void pointerDown(float x, float y, Sint64 id);
    void pointerMove(float x, float y, Sint64 id);
    void pointerUp(float x, float y, Sint64 id);
    void releaseUiPointer(float x, float y);
    void activate(const Button& button);
    bool goBack();

    void wireEvents();
    void flushPad();
    void showDiscover(const std::string& status = std::string(),
                      bool isError = false);
    void beginConnect(const std::string& ip, uint16_t port);

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    bool running_ = true;

    int width_ = 0;
    int height_ = 0;
    float u_ = 1.0f;
    int fsTiny_ = 1;
    int fsSmall_ = 2;
    int fsBody_ = 2;
    int fsTitle_ = 4;
    int fsHuge_ = 6;

    Screen screen_ = Screen::Discover;
    std::vector<Button> buttons_;
    Button pressedButton_;
    bool hasPressedButton_ = false;
    bool hasPointer_ = false;
    Sint64 pointerId_ = 0;
    float pointerX_ = 0.0f;
    float pointerY_ = 0.0f;
    bool mouseDown_ = false;

    NetClient net_;
    DiscoveryClient discovery_;
    Touchpad pad_;
    std::string deviceName_ = "ANDROID TOUCHPAD";

    std::string status_ = "SEARCHING FOR YOUR PC...";
    bool statusError_ = false;

    std::string manualIp_ = "192.168.1.";
    std::string pin_;
    std::string pinError_;
    std::string pinServerName_;

    float sensitivity_ = 1.5f;
    RotationMode rotationMode_ = RotationMode::Auto;
    float moveAccX_ = 0.0f;
    float moveAccY_ = 0.0f;

    Rect listArea_;
    Rect manualIpRect_;
    Rect padArea_;
    std::vector<Rect> pinBoxes_;
    int hiddenServers_ = 0;

    float discoverTitleY_ = 0.0f;
    float discoverRuleY_ = 0.0f;
    float discoverStatusY_ = 0.0f;
    float discoverLabelY_ = 0.0f;
    float pinNameY_ = 0.0f;
    float pinPromptY_ = 0.0f;
    float pinMessageY_ = 0.0f;
    float pinAttemptsY_ = 0.0f;
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool App::init() {
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "sensor");
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        SDL_Log("Remote Mouse: SDL_Init failed: %s", SDL_GetError());
        return false;
    }

#if defined(__ANDROID__)
    window_ =
        SDL_CreateWindow("Remote Mouse", 0, 0, 0, 0, SDL_WINDOW_FULLSCREEN);
#else
    window_ = SDL_CreateWindow("Remote Mouse", SDL_WINDOWPOS_CENTERED,
                               SDL_WINDOWPOS_CENTERED, 420, 840,
                               SDL_WINDOW_SHOWN);
#endif
    if (window_ == nullptr) {
        SDL_Log("Remote Mouse: SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }

    renderer_ = SDL_CreateRenderer(
        window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (renderer_ == nullptr) {
        renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
    }
    if (renderer_ == nullptr) {
        SDL_Log("Remote Mouse: SDL_CreateRenderer failed: %s", SDL_GetError());
        return false;
    }

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_GetWindowSize(window_, &width_, &height_);
    if (width_ <= 0 || height_ <= 0) {
        width_ = 420;
        height_ = 840;
    }

    // Apply the initial rotation mode so the app starts in the chosen
    // orientation (Auto follows the device sensor).
    applyRotationMode();

    SDL_DisableScreenSaver();
    discovery_.start(mctl::kDefaultDiscoveryPort);
    return true;
}

void App::shutdown() {
    discovery_.stop();
    net_.disconnect();
    if (renderer_ != nullptr) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
    if (window_ != nullptr) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    SDL_Quit();
}

int App::run() {
    if (!init()) {
        shutdown();
        return 1;
    }

    while (running_) {
        const Uint32 started = SDL_GetTicks();
        frame();
        const Uint32 elapsed = SDL_GetTicks() - started;
        if (elapsed < 16) {
            SDL_Delay(16 - elapsed);
        }
    }

    shutdown();
    return 0;
}

void App::frame() {
    // Lay out before input so hit testing always sees the current screen.
    updateLayout();

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        handleEvent(event);
    }

    discovery_.poll();
    net_.poll();
    wireEvents();
    flushPad();

    // A network event or a button may have changed the screen.
    updateLayout();
    render();
}

// ---------------------------------------------------------------------------
// Navigation / network
// ---------------------------------------------------------------------------

void App::showDiscover(const std::string& status, bool isError) {
    screen_ = Screen::Discover;
    status_ = status.empty() ? "TAP A PC TO CONNECT" : status;
    statusError_ = isError;

    discovery_.start(mctl::kDefaultDiscoveryPort);

    hasPointer_ = false;
    hasPressedButton_ = false;

    pad_.cancel();
    pad_.takeEvents();
    moveAccX_ = 0.0f;
    moveAccY_ = 0.0f;

    pin_.clear();
    pinError_.clear();
}

void App::beginConnect(const std::string& ip, uint16_t port) {
    discovery_.stop();

    pad_.cancel();
    pad_.takeEvents();
    moveAccX_ = 0.0f;
    moveAccY_ = 0.0f;

    pin_.clear();
    pinError_.clear();
    pinServerName_.clear();

    screen_ = Screen::Discover;
    status_ = "CONNECTING TO " + ip + "...";
    statusError_ = false;

    net_.connectTo(ip, port, deviceName_);
}

bool App::goBack() {
    switch (screen_) {
        case Screen::Discover:
            return false;
        case Screen::Manual:
            showDiscover();
            return true;
        case Screen::Pin:
        case Screen::Pad:
            net_.disconnect();
            showDiscover(screen_ == Screen::Pad ? "DISCONNECTED" : std::string());
            return true;
    }
    return false;
}

void App::wireEvents() {
    const std::vector<NetEvent> events = net_.takeEvents();

    for (const NetEvent& event : events) {
        switch (event.type) {
            case NetEvent::Type::AuthNeeded:
                pin_.clear();
                pinError_.clear();
                pinServerName_ = event.text;
                screen_ = Screen::Pin;
                hasPointer_ = false;
                hasPressedButton_ = false;
                break;

            case NetEvent::Type::AuthOk:
                screen_ = Screen::Pad;
                pad_.cancel();
                pad_.takeEvents();
                moveAccX_ = 0.0f;
                moveAccY_ = 0.0f;
                hasPointer_ = false;
                hasPressedButton_ = false;
                break;

            case NetEvent::Type::AuthFailed:
                pin_.clear();
                pinError_ = event.text.empty() ? "WRONG PIN" : event.text;
                if (event.value <= 0) {
                    showDiscover(pinError_, true);
                }
                break;

            case NetEvent::Type::Closed:
                if (screen_ == Screen::Discover) {
                    if (!event.text.empty()) {
                        status_ = event.text;
                        statusError_ = true;
                    }
                } else {
                    showDiscover(event.text.empty() ? "DISCONNECTED" : event.text,
                                 true);
                }
                break;

            case NetEvent::Type::Failed:
                showDiscover(
                    event.text.empty() ? "CONNECTION FAILED" : event.text, true);
                break;

            case NetEvent::Type::Latency:
                break;
        }
    }
}

void App::flushPad() {
    const std::vector<PadEvent> events = pad_.takeEvents();
    if (events.empty() || !net_.ready()) return;

    for (const PadEvent& event : events) {
        switch (event.kind) {
            case PadEvent::Kind::Move: {
                moveAccX_ += event.dx * sensitivity_;
                moveAccY_ += event.dy * sensitivity_;

                const int dx = static_cast<int>(std::trunc(moveAccX_));
                const int dy = static_cast<int>(std::trunc(moveAccY_));
                moveAccX_ -= static_cast<float>(dx);
                moveAccY_ -= static_cast<float>(dy);

                if (dx != 0 || dy != 0) {
                    net_.sendMove(dx, dy);
                }
                break;
            }

            case PadEvent::Kind::Button:
                net_.sendButton(event.button, event.pressed);
                break;

            case PadEvent::Kind::Wheel: {
                // Natural scrolling: fingers down scrolls the page down.
                const int notches = static_cast<int>(std::lround(-event.dy));
                if (notches != 0) {
                    net_.sendWheel(0, notches);
                }
                break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void App::updateLayout() {
    int w = width_;
    int h = height_;
    SDL_GetWindowSize(window_, &w, &h);
    if (w < 1) w = 1;
    if (h < 1) h = 1;

    if (w != width_ || h != height_) {
        width_ = w;
        height_ = h;
        SDL_RenderSetLogicalSize(renderer_, width_, height_);
    }

    u_ = std::clamp(static_cast<float>(std::min(width_, height_)) / 400.0f,
                    0.8f, 3.2f);

    fsTiny_ = std::max(1, static_cast<int>(std::lround(u_ * 0.9f)));
    fsSmall_ = std::max(1, static_cast<int>(std::lround(u_ * 1.2f)));
    fsBody_ = std::max(2, static_cast<int>(std::lround(u_ * 1.5f)));
    fsTitle_ = std::max(3, static_cast<int>(std::lround(u_ * 2.2f)));
    fsHuge_ = std::max(4, static_cast<int>(std::lround(u_ * 3.0f)));

    // Gesture thresholds follow the screen size so taps stay forgiving on a
    // small phone and do not become sluggish on a tablet.
    pad_.configure(18.0f * u_, 60.0f * u_, 400);

    buttons_.clear();

    switch (screen_) {
        case Screen::Discover:
            layoutDiscover();
            break;
        case Screen::Manual:
            layoutManual();
            break;
        case Screen::Pin:
            layoutPin();
            break;
        case Screen::Pad:
            layoutPad();
            break;
    }
}

void App::layoutDiscover() {
    const float m = 18.0f * u_;

    float y = 26.0f * u_;
    discoverTitleY_ = y;
    y += ui::textHeight(fsHuge_) + 10.0f * u_;
    discoverRuleY_ = y;
    y += 3.0f * u_ + 14.0f * u_;
    discoverStatusY_ = y;
    y += ui::textHeight(fsSmall_) + 24.0f * u_;
    discoverLabelY_ = y;
    y += ui::textHeight(fsTiny_) + 10.0f * u_;

    const float footerTop = height_ - m - ui::textHeight(fsTiny_);
    const float manualHeight = 46.0f * u_;
    const float manualY = footerTop - 16.0f * u_ - manualHeight;

    listArea_ =
        Rect{m, y, width_ - 2.0f * m,
             std::max(0.0f, manualY - 14.0f * u_ - y)};

    const std::vector<DiscoveredServer>& servers = discovery_.servers();
    const float cardHeight = 64.0f * u_;
    const float gap = 9.0f * u_;

    int visible = 0;
    float cardY = listArea_.y;

    for (const DiscoveredServer& server : servers) {
        if (cardY + cardHeight > listArea_.y + listArea_.h) break;

        Button card;
        card.rect = Rect{listArea_.x, cardY, listArea_.w, cardHeight};
        card.action = Action::PickServer;
        card.value = server.port;
        card.data = server.ip;
        card.label = server.name.empty() ? std::string("REMOTE PC") : server.name;
        card.sub = server.ip + ":" + std::to_string(server.port);
        card.textScale = fsBody_;
        card.look = Look::Card;
        buttons_.push_back(card);

        cardY += cardHeight + gap;
        ++visible;
    }

    hiddenServers_ = static_cast<int>(servers.size()) - visible;

    Button manual;
    manual.rect = Rect{m, manualY, width_ - 2.0f * m, manualHeight};
    manual.action = Action::OpenManual;
    manual.label = "ENTER IP MANUALLY";
    manual.textScale = fsBody_;
    manual.look = Look::Ghost;
    buttons_.push_back(manual);
}

void App::layoutManual() {
    const float m = 18.0f * u_;

    Button back;
    back.rect = Rect{m, 18.0f * u_, 46.0f * u_, 46.0f * u_};
    back.action = Action::GoBack;
    back.label = "<";
    back.textScale = fsTitle_;
    back.look = Look::Ghost;
    buttons_.push_back(back);

    const float ipY = 18.0f * u_ + 46.0f * u_ + 26.0f * u_;
    const float ipHeight = 58.0f * u_;
    manualIpRect_ = Rect{m, ipY, width_ - 2.0f * m, ipHeight};

    const float connectHeight = 50.0f * u_;
    const float connectY = height_ - m - 22.0f * u_ - connectHeight;

    Button connect;
    connect.rect = Rect{m, connectY, width_ - 2.0f * m, connectHeight};
    connect.action = Action::ManualConnect;
    connect.label = "CONNECT";
    connect.textScale = fsBody_ + 1;
    connect.look = Look::Primary;
    buttons_.push_back(connect);

    layoutKeypad(ipY + ipHeight + 22.0f * u_, connectY - 16.0f * u_,
                 Action::ManualDigit, Action::ManualBackspace, true);
}

void App::layoutPin() {
    const float m = 18.0f * u_;

    Button back;
    back.rect = Rect{m, 18.0f * u_, 46.0f * u_, 46.0f * u_};
    back.action = Action::GoBack;
    back.label = "<";
    back.textScale = fsTitle_;
    back.look = Look::Ghost;
    buttons_.push_back(back);

    pinNameY_ =
        18.0f * u_ + (46.0f * u_ - ui::textHeight(fsBody_)) * 0.5f;

    float y = 18.0f * u_ + 46.0f * u_ + 22.0f * u_;
    pinPromptY_ = y;
    y += ui::textHeight(fsSmall_) + 20.0f * u_;

    pinBoxes_.clear();
    const float gap = 8.0f * u_;
    const float boxWidth = (width_ - 2.0f * m - 5.0f * gap) / 6.0f;
    const float boxHeight = 62.0f * u_;

    for (int i = 0; i < 6; ++i) {
        pinBoxes_.push_back(Rect{m + static_cast<float>(i) * (boxWidth + gap),
                                 y, boxWidth, boxHeight});
    }
    y += boxHeight + 14.0f * u_;

    pinMessageY_ = y;
    y += ui::textHeight(fsSmall_) + 6.0f * u_;
    pinAttemptsY_ = y;
    y += ui::textHeight(fsTiny_) + 14.0f * u_;

    const float keypadBottom =
        height_ - m - ui::textHeight(fsTiny_) - 12.0f * u_;
    layoutKeypad(y, keypadBottom, Action::PinDigit, Action::PinBackspace,
                 false);
}

void App::layoutPad() {
    const float m = 14.0f * u_;

    Button close;
    close.rect = Rect{width_ - m - 44.0f * u_, 12.0f * u_, 44.0f * u_,
                      44.0f * u_};
    close.action = Action::Disconnect;
    close.label = "X";
    close.textScale = fsBody_ + 1;
    close.look = Look::Ghost;
    buttons_.push_back(close);

    Button rotate;
    rotate.rect = Rect{width_ - m - 44.0f * u_ - 52.0f * u_, 12.0f * u_,
                       44.0f * u_, 44.0f * u_};
    rotate.action = Action::Rotate;
    rotate.label = rotationLabel();
    rotate.textScale = fsBody_;
    rotate.look = Look::Ghost;
    buttons_.push_back(rotate);

    const float headerHeight = 54.0f * u_;
    const float footerHeight = 44.0f * u_;
    const float footerTop = height_ - m - footerHeight;

    padArea_ = Rect{m, headerHeight + 10.0f * u_, width_ - 2.0f * m,
                    std::max(0.0f, footerTop - 10.0f * u_ -
                                       (headerHeight + 10.0f * u_))};

    const float size = 36.0f * u_;
    const float buttonY = footerTop + (footerHeight - size) * 0.5f;

    Button minus;
    minus.rect = Rect{m + 28.0f * u_, buttonY, size, size};
    minus.action = Action::SensitivityDown;
    minus.label = "-";
    minus.textScale = fsTitle_;
    minus.look = Look::Ghost;
    buttons_.push_back(minus);

    Button plus;
    plus.rect = Rect{m + 124.0f * u_, buttonY, size, size};
    plus.action = Action::SensitivityUp;
    plus.label = "+";
    plus.textScale = fsTitle_;
    plus.look = Look::Ghost;
    buttons_.push_back(plus);
}

void App::layoutKeypad(float top, float bottom, Action digitAction,
                       Action backAction, bool includeDot) {
    const float m = 18.0f * u_;
    const float gap = 9.0f * u_;
    const float keyWidth = (width_ - 2.0f * m - 2.0f * gap) / 3.0f;

    float keyHeight = (bottom - top - 3.0f * gap) / 4.0f;
    keyHeight = std::max(24.0f * u_, keyHeight);
    keyHeight = std::min(keyHeight, keyWidth * 0.85f);

    const float gridHeight = keyHeight * 4.0f + gap * 3.0f;
    float gridY = top + (bottom - top - gridHeight) * 0.5f;
    if (gridY < top) gridY = top;

    static const char* const labels[12] = {
        "1", "2", "3", "4", "5", "6", "7", "8", "9", ".", "0", "<",
    };

    for (int i = 0; i < 12; ++i) {
        if (i == 9 && !includeDot) continue;

        const int column = i % 3;
        const int row = i / 3;

        Button key;
        key.rect = Rect{m + static_cast<float>(column) * (keyWidth + gap),
                        gridY + static_cast<float>(row) * (keyHeight + gap),
                        keyWidth, keyHeight};
        key.textScale = fsHuge_;
        key.look = Look::Secondary;

        if (labels[i][0] == '<') {
            key.action = backAction;
            key.label = "<";
            key.look = Look::Ghost;
        } else if (labels[i][0] == '.') {
            key.action = Action::ManualDot;
            key.label = ".";
        } else {
            key.action = digitAction;
            key.value = labels[i][0] - '0';
            key.label = labels[i];
        }

        buttons_.push_back(key);
    }
}

void App::applyRotationMode() {
#ifdef __ANDROID__
    int orientation = 0;  // SENSOR
    switch (rotationMode_) {
        case RotationMode::Auto:
            orientation = 0;   // SCREEN_ORIENTATION_SENSOR
            break;
        case RotationMode::Portrait:
            orientation = 1;   // SCREEN_ORIENTATION_PORTRAIT
            break;
        case RotationMode::Landscape:
            orientation = 2;   // SCREEN_ORIENTATION_LANDSCAPE
            break;
    }
    setScreenOrientation(orientation);
#else
    (void)0;
#endif
}

std::string App::rotationLabel() const {
    switch (rotationMode_) {
        case RotationMode::Auto:
            return "AUTO";
        case RotationMode::Portrait:
            return "PORT";
        case RotationMode::Landscape:
            return "LAND";
    }
    return "AUTO";
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void App::pointerDown(float x, float y, Sint64 id) {
    if (screen_ == Screen::Pad) {
        if (!hasPointer_) {
            for (const Button& button : buttons_) {
                if (button.enabled && button.rect.contains(x, y)) {
                    hasPointer_ = true;
                    hasPressedButton_ = true;
                    pointerId_ = id;
                    pointerX_ = x;
                    pointerY_ = y;
                    pressedButton_ = button;
                    return;
                }
            }
        }
        if (padArea_.contains(x, y)) {
            pad_.begin(id, x, y, nowMs());
        }
        return;
    }

    if (hasPointer_) return;

    hasPointer_ = true;
    hasPressedButton_ = false;
    pointerId_ = id;
    pointerX_ = x;
    pointerY_ = y;

    for (const Button& button : buttons_) {
        if (button.enabled && button.rect.contains(x, y)) {
            pressedButton_ = button;
            hasPressedButton_ = true;
            break;
        }
    }
}

void App::pointerMove(float x, float y, Sint64 id) {
    if (screen_ == Screen::Pad) {
        if (hasPointer_ && id == pointerId_) {
            pointerX_ = x;
            pointerY_ = y;
            return;
        }
        pad_.move(id, x, y, nowMs());
        return;
    }

    if (!hasPointer_ || id != pointerId_) return;
    pointerX_ = x;
    pointerY_ = y;
}

void App::pointerUp(float x, float y, Sint64 id) {
    if (screen_ == Screen::Pad) {
        if (hasPointer_ && id == pointerId_) {
            releaseUiPointer(x, y);
            return;
        }
        pad_.end(id, x, y, nowMs());
        return;
    }

    if (!hasPointer_ || id != pointerId_) return;
    releaseUiPointer(x, y);
}

void App::releaseUiPointer(float x, float y) {
    pointerX_ = x;
    pointerY_ = y;

    const bool fire = hasPressedButton_ && pressedButton_.enabled &&
                      pressedButton_.rect.contains(x, y);
    Button chosen = pressedButton_;

    hasPointer_ = false;
    hasPressedButton_ = false;

    if (fire) {
        activate(chosen);
    }
}

void App::activate(const Button& button) {
    switch (button.action) {
        case Action::PickServer:
            beginConnect(button.data, static_cast<uint16_t>(button.value));
            break;

        case Action::OpenManual:
            screen_ = Screen::Manual;
            break;

        case Action::ManualDigit:
            if (manualIp_.size() < 15) {
                manualIp_.push_back(static_cast<char>('0' + button.value));
            }
            break;

        case Action::ManualDot:
            if (manualIp_.size() < 15) {
                manualIp_.push_back('.');
            }
            break;

        case Action::ManualBackspace:
            if (!manualIp_.empty()) {
                manualIp_.pop_back();
            }
            break;

        case Action::ManualConnect: {
            if (manualIp_.empty()) break;
            const std::string ip = manualIp_;
            beginConnect(ip, mctl::kDefaultPort);
            break;
        }

        case Action::PinDigit:
            if (pin_.size() < 6) {
                pin_.push_back(static_cast<char>('0' + button.value));
                pinError_.clear();

                if (pin_.size() == 6) {
                    uint32_t pin = 0;
                    for (const char digit : pin_) {
                        pin = pin * 10 + static_cast<uint32_t>(digit - '0');
                    }
                    net_.submitPin(pin);
                }
            }
            break;

        case Action::PinBackspace:
            if (!pin_.empty()) {
                pin_.pop_back();
            }
            pinError_.clear();
            break;

        case Action::Disconnect:
            net_.disconnect();
            showDiscover("DISCONNECTED");
            break;

        case Action::SensitivityDown:
            sensitivity_ =
                std::max(kMinSensitivity, sensitivity_ - kSensitivityStep);
            break;

        case Action::SensitivityUp:
            sensitivity_ =
                std::min(kMaxSensitivity, sensitivity_ + kSensitivityStep);
            break;

        case Action::Rotate: {
            rotationMode_ = static_cast<RotationMode>(
                (static_cast<int>(rotationMode_) + 1) %
                static_cast<int>(RotationMode::Landscape) + 1);
            applyRotationMode();
            break;
        }

        case Action::GoBack:
            goBack();
            break;

        case Action::None:
            break;
    }
}

void App::handleEvent(const SDL_Event& event) {
    switch (event.type) {
        case SDL_QUIT:
            running_ = false;
            break;

        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
                running_ = false;
            }
            break;

        case SDL_APP_WILLENTERBACKGROUND:
            // Never leave a drag latched on the PC while we are suspended.
            pad_.cancel();
            flushPad();
            break;

        case SDL_APP_DIDENTERFOREGROUND:
            net_.resume();
            break;

        case SDL_FINGERDOWN:
            pointerDown(event.tfinger.x * static_cast<float>(width_),
                        event.tfinger.y * static_cast<float>(height_),
                        static_cast<Sint64>(event.tfinger.fingerId));
            break;

        case SDL_FINGERMOTION:
            pointerMove(event.tfinger.x * static_cast<float>(width_),
                        event.tfinger.y * static_cast<float>(height_),
                        static_cast<Sint64>(event.tfinger.fingerId));
            break;

        case SDL_FINGERUP:
            pointerUp(event.tfinger.x * static_cast<float>(width_),
                      event.tfinger.y * static_cast<float>(height_),
                      static_cast<Sint64>(event.tfinger.fingerId));
            break;

        case SDL_MOUSEBUTTONDOWN:
            if (event.button.button == SDL_BUTTON_LEFT) {
                mouseDown_ = true;
                pointerDown(static_cast<float>(event.button.x),
                            static_cast<float>(event.button.y),
                            kMousePointerId);
            }
            break;

        case SDL_MOUSEBUTTONUP:
            if (event.button.button == SDL_BUTTON_LEFT) {
                mouseDown_ = false;
                pointerUp(static_cast<float>(event.button.x),
                          static_cast<float>(event.button.y), kMousePointerId);
            }
            break;

        case SDL_MOUSEMOTION:
            if (mouseDown_) {
                pointerMove(static_cast<float>(event.motion.x),
                            static_cast<float>(event.motion.y),
                            kMousePointerId);
            }
            break;

        case SDL_KEYDOWN:
            if (event.key.repeat) break;
            if (event.key.keysym.scancode == SDL_SCANCODE_AC_BACK ||
                event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                if (!goBack()) {
                    running_ = false;
                }
            }
            break;

        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void App::render() {
    SDL_SetRenderDrawColor(renderer_, ui::colors::background.r,
                           ui::colors::background.g,
                           ui::colors::background.b,
                           ui::colors::background.a);
    SDL_RenderClear(renderer_);

    switch (screen_) {
        case Screen::Discover:
            renderDiscover();
            break;
        case Screen::Manual:
            renderManual();
            break;
        case Screen::Pin:
            renderPin();
            break;
        case Screen::Pad:
            renderPad();
            break;
    }

    drawButtons();
    SDL_RenderPresent(renderer_);
}

void App::renderDiscover() {
    const float m = 18.0f * u_;

    ui::text(renderer_, m, discoverTitleY_, "REMOTE", fsHuge_,
             ui::colors::text);
    ui::text(renderer_, m + ui::textWidth("REMOTE ", fsHuge_), discoverTitleY_,
             "MOUSE", fsHuge_, ui::colors::accent);
    ui::fillRoundedRect(renderer_,
                        Rect{m, discoverRuleY_, 48.0f * u_, 3.0f * u_},
                        1.5f * u_, ui::colors::accent);

    SDL_Color statusColor = ui::colors::textMuted;
    if (statusError_) {
        statusColor = ui::colors::danger;
    } else if (net_.active()) {
        statusColor = ui::colors::accent;
    }
    ui::text(renderer_, m, discoverStatusY_,
             fitText(status_, width_ - 2.0f * m, fsSmall_), fsSmall_,
             statusColor);

    ui::text(renderer_, m, discoverLabelY_, "AVAILABLE PCS", fsTiny_,
             ui::colors::textMuted);

    const std::vector<DiscoveredServer>& servers = discovery_.servers();

    if (!servers.empty()) {
        std::string count = std::to_string(servers.size()) + " FOUND";
        if (hiddenServers_ > 0) {
            count = std::to_string(static_cast<int>(servers.size()) -
                                   hiddenServers_) +
                    " OF " + std::to_string(servers.size());
        }
        ui::text(renderer_, width_ - m - ui::textWidth(count, fsTiny_),
                 discoverLabelY_, count, fsTiny_, ui::colors::accent);
        return;
    }

    const float centerY = listArea_.centerY();

    const std::string headline =
        discovery_.running() ? "SEARCHING FOR YOUR PC..." : "SEARCH UNAVAILABLE";
    ui::textCentered(
        renderer_,
        Rect{listArea_.x, centerY - 46.0f * u_, listArea_.w,
             ui::textHeight(fsBody_)},
        headline, fsBody_, ui::colors::text);

    ui::textCentered(
        renderer_,
        Rect{listArea_.x, centerY + 4.0f * u_, listArea_.w,
             ui::textHeight(fsTiny_)},
        discovery_.running() ? "START REMOTE_MOUSE_SERVER ON THE PC"
                             : "TAP \"ENTER IP MANUALLY\" BELOW",
        fsTiny_, ui::colors::textMuted);

    ui::textCentered(
        renderer_,
        Rect{listArea_.x, centerY + 20.0f * u_, listArea_.w,
             ui::textHeight(fsTiny_)},
        "BOTH DEVICES MUST USE THE SAME WIFI", fsTiny_,
        ui::colors::textMuted);
}

void App::renderManual() {
    const float m = 18.0f * u_;

    ui::text(renderer_, m + 60.0f * u_,
             18.0f * u_ + (46.0f * u_ - ui::textHeight(fsBody_)) * 0.5f,
             "MANUAL CONNECTION", fsBody_, ui::colors::text);

    ui::fillRoundedRect(renderer_, manualIpRect_, 12.0f * u_,
                        ui::colors::border);
    ui::fillRoundedRect(renderer_, inset(manualIpRect_, 2.0f * u_), 10.0f * u_,
                        ui::colors::surface);

    const bool empty = manualIp_.empty();
    ui::textCentered(renderer_, manualIpRect_, empty ? "0.0.0.0" : manualIp_,
                     fsTitle_, empty ? ui::colors::textMuted
                                     : ui::colors::text);

    const std::string portText =
        "PORT " + std::to_string(static_cast<unsigned>(mctl::kDefaultPort));
    ui::textCentered(
        renderer_,
        Rect{0, manualIpRect_.y + manualIpRect_.h + 5.0f * u_,
             static_cast<float>(width_), ui::textHeight(fsTiny_)},
        portText, fsTiny_, ui::colors::textMuted);

    ui::textCentered(
        renderer_,
        Rect{0, height_ - m - ui::textHeight(fsTiny_),
             static_cast<float>(width_), ui::textHeight(fsTiny_)},
        "TIP: THE SERVER PRINTS ITS IP AT STARTUP", fsTiny_,
        ui::colors::textMuted);
}

void App::renderPin() {
    const float m = 18.0f * u_;
    const float nameX = m + 60.0f * u_;

    ui::text(renderer_, nameX, pinNameY_,
             fitText(pinServerName_.empty() ? "REMOTE PC" : pinServerName_,
                     width_ - nameX - m, fsBody_),
             fsBody_, ui::colors::text);

    ui::textCentered(
        renderer_,
        Rect{0, pinPromptY_, static_cast<float>(width_),
             ui::textHeight(fsSmall_)},
        "ENTER THE PIN SHOWN ON YOUR PC", fsSmall_, ui::colors::textMuted);

    for (size_t i = 0; i < pinBoxes_.size(); ++i) {
        const Rect& box = pinBoxes_[i];
        const bool filled = i < pin_.size();
        const bool active = i == pin_.size();

        ui::fillRoundedRect(renderer_, box, 10.0f * u_,
                            active ? ui::colors::accent : ui::colors::border);
        ui::fillRoundedRect(renderer_, inset(box, 2.0f * u_), 8.0f * u_,
                            filled ? ui::colors::surfaceRaised
                                   : ui::colors::surface);

        if (filled) {
            ui::textCentered(renderer_, box, std::string(1, pin_[i]), fsHuge_,
                             ui::colors::text);
        } else if (active) {
            const float dot = 6.0f * u_;
            ui::fillRoundedRect(
                renderer_,
                Rect{box.centerX() - dot * 0.5f, box.centerY() + 12.0f * u_,
                     dot, dot},
                dot * 0.5f, ui::colors::accent);
        }
    }

    if (!pinError_.empty()) {
        ui::textCentered(renderer_,
                         Rect{0, pinMessageY_, static_cast<float>(width_),
                              ui::textHeight(fsSmall_)},
                         fitText(pinError_, width_ - 2.0f * m, fsSmall_),
                         fsSmall_, ui::colors::danger);
    }

    if (net_.active()) {
        const std::string attempts =
            "ATTEMPTS LEFT: " + std::to_string(net_.attemptsLeft());
        ui::textCentered(renderer_,
                         Rect{0, pinAttemptsY_, static_cast<float>(width_),
                              ui::textHeight(fsTiny_)},
                         attempts, fsTiny_, ui::colors::textMuted);
    }

    ui::textCentered(
        renderer_,
        Rect{0, height_ - m - ui::textHeight(fsTiny_),
             static_cast<float>(width_), ui::textHeight(fsTiny_)},
        "PIN CHANGES WHEN THE SERVER RESTARTS", fsTiny_,
        ui::colors::textMuted);
}

void App::renderPad() {
    const float m = 14.0f * u_;

    const std::string name =
        net_.serverName().empty() ? "REMOTE PC" : net_.serverName();
    ui::text(renderer_, m, 15.0f * u_,
             fitText(name, width_ - 2.0f * m - 52.0f * u_, fsBody_), fsBody_,
             ui::colors::text);

    const float subY = 15.0f * u_ + ui::textHeight(fsBody_) + 5.0f * u_;
    const float dot = 7.0f * u_;
    ui::fillRoundedRect(
        renderer_,
        Rect{m, subY + (ui::textHeight(fsTiny_) - dot) * 0.5f, dot, dot},
        dot * 0.5f, ui::colors::success);
    ui::text(renderer_, m + dot + 6.0f * u_, subY, "PAIRED", fsTiny_,
             ui::colors::success);

    // Touch surface.
    ui::fillRoundedRect(renderer_, padArea_, 18.0f * u_, ui::colors::border);
    ui::fillRoundedRect(renderer_, inset(padArea_, 2.0f * u_), 16.0f * u_,
                        ui::colors::surface);

    // Corner brackets.
    const float bracket = 22.0f * u_;
    const float edge = 16.0f * u_;
    const float thickness = 2.0f * u_;
    const SDL_Color mark = ui::colors::accentDim;
    const float left = padArea_.x + edge;
    const float right = padArea_.x + padArea_.w - edge;
    const float top = padArea_.y + edge;
    const float bottom = padArea_.y + padArea_.h - edge;

    ui::fillRect(renderer_, Rect{left, top, bracket, thickness}, mark);
    ui::fillRect(renderer_, Rect{left, top, thickness, bracket}, mark);
    ui::fillRect(renderer_, Rect{right - bracket, top, bracket, thickness},
                 mark);
    ui::fillRect(renderer_, Rect{right - thickness, top, thickness, bracket},
                 mark);
    ui::fillRect(renderer_, Rect{left, bottom - thickness, bracket, thickness},
                 mark);
    ui::fillRect(renderer_, Rect{left, bottom - bracket, thickness, bracket},
                 mark);
    ui::fillRect(
        renderer_, Rect{right - bracket, bottom - thickness, bracket, thickness},
        mark);
    ui::fillRect(
        renderer_, Rect{right - thickness, bottom - bracket, thickness, bracket},
        mark);

    // Gesture hints inside the pad.
    const float hintY = padArea_.y + padArea_.h - 60.0f * u_;
    ui::textCentered(
        renderer_,
        Rect{padArea_.x, hintY, padArea_.w, ui::textHeight(fsTiny_)},
        "TAP = LEFT   2F = RIGHT   3F = MIDDLE", fsTiny_,
        ui::colors::textMuted);
    ui::textCentered(
        renderer_,
        Rect{padArea_.x, hintY + 12.0f * u_ + ui::textHeight(fsTiny_),
             padArea_.w, ui::textHeight(fsTiny_)},
        "DRAG = MOVE   2F DRAG = SCROLL", fsTiny_, ui::colors::textMuted);

    // Footer: sensitivity controls and latency.
    const float footerHeight = 44.0f * u_;
    const float footerTop = height_ - m - footerHeight;
    const float centerY = footerTop + footerHeight * 0.5f;

    ui::text(renderer_, m, centerY - ui::textHeight(fsTiny_) * 0.5f, "SENS",
             fsTiny_, ui::colors::textMuted);

    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%.1fX",
                  static_cast<double>(sensitivity_));
    ui::text(renderer_, m + 72.0f * u_,
             centerY - ui::textHeight(fsSmall_) * 0.5f, buffer, fsSmall_,
             ui::colors::text);

    const int latency = net_.latencyMs();
    const std::string latencyText =
        latency >= 0 ? std::to_string(latency) + " MS" : "-- MS";
    SDL_Color latencyColor = ui::colors::textMuted;
    if (latency >= 0 && latency < 40) {
        latencyColor = ui::colors::success;
    } else if (latency >= 120) {
        latencyColor = ui::colors::danger;
    }
    ui::text(renderer_,
             width_ - m - ui::textWidth(latencyText, fsTiny_),
             centerY - ui::textHeight(fsTiny_) * 0.5f, latencyText, fsTiny_,
             latencyColor);
}

void App::drawButtons() {
    const float radius = 12.0f * u_;
    const float borderWidth = 2.0f * u_;

    for (const Button& button : buttons_) {
        const bool pressed =
            hasPointer_ && hasPressedButton_ &&
            button.rect.x == pressedButton_.rect.x &&
            button.rect.y == pressedButton_.rect.y;

        SDL_Color fill = ui::colors::surfaceRaised;
        SDL_Color textColor = ui::colors::text;
        SDL_Color borderColor = ui::colors::border;

        switch (button.look) {
            case Look::Card:
                fill = pressed ? ui::colors::surfaceRaised : ui::colors::surface;
                borderColor =
                    pressed ? ui::colors::accent : ui::colors::border;
                break;
            case Look::Primary:
                fill = pressed ? ui::colors::accentDim : ui::colors::accent;
                borderColor = fill;
                textColor = ui::colors::background;
                break;
            case Look::Secondary:
                fill = pressed ? ui::colors::border : ui::colors::surfaceRaised;
                break;
            case Look::Ghost:
                fill = pressed ? ui::colors::surfaceRaised
                               : ui::colors::background;
                borderColor =
                    pressed ? ui::colors::accent : ui::colors::border;
                break;
            case Look::Danger:
                fill = pressed ? SDL_Color{69, 26, 26, 255} : ui::colors::surface;
                borderColor = ui::colors::danger;
                textColor = ui::colors::danger;
                break;
        }

        if (!button.enabled) {
            textColor = ui::colors::textMuted;
        }

        if (button.look == Look::Ghost) {
            ui::fillRoundedRect(renderer_, button.rect, radius, borderColor);
            ui::fillRoundedRect(renderer_, inset(button.rect, borderWidth),
                                std::max(0.0f, radius - borderWidth), fill);
        } else {
            ui::fillRoundedRect(renderer_, button.rect, radius, fill);
            if (button.look != Look::Primary) {
                ui::fillRoundedRect(renderer_, button.rect, radius,
                                    borderColor);
                ui::fillRoundedRect(renderer_, inset(button.rect, borderWidth),
                                    std::max(0.0f, radius - borderWidth), fill);
            }
        }

        if (button.look == Look::Card) {
            const float padding = 16.0f * u_;
            const float textLimit =
                button.rect.w - padding * 2.0f - 22.0f * u_;

            ui::text(renderer_, button.rect.x + padding,
                     button.rect.y + 12.0f * u_,
                     fitText(button.label, textLimit, button.textScale),
                     button.textScale, textColor);

            ui::text(renderer_, button.rect.x + padding,
                     button.rect.y + button.rect.h - 12.0f * u_ -
                         ui::textHeight(fsTiny_),
                     fitText(button.sub, textLimit, fsTiny_), fsTiny_,
                     ui::colors::textMuted);

            const float dot = 9.0f * u_;
            ui::fillRoundedRect(
                renderer_,
                Rect{button.rect.x + button.rect.w - padding - dot,
                     button.rect.centerY() - dot * 0.5f, dot, dot},
                dot * 0.5f, ui::colors::success);
        } else if (!button.label.empty()) {
            ui::textCentered(renderer_, button.rect, button.label,
                             button.textScale, textColor);
        }
    }
}

}  // namespace

int main(int, char**) {
    App app;
    return app.run();
}
