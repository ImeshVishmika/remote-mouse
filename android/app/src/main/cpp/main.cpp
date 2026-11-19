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
#include "server_share.h"
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
// android.content.pm.ActivityInfo screen orientation values.
// Note: LANDSCAPE is 0 and PORTRAIT is 1, so using 0 for "sensor" would
// lock the whole activity to landscape.
constexpr int kOrientSensor = 4;           // SCREEN_ORIENTATION_SENSOR
constexpr int kOrientSensorLandscape = 6;  // SCREEN_ORIENTATION_SENSOR_LANDSCAPE
constexpr int kOrientSensorPortrait = 7;   // SCREEN_ORIENTATION_SENSOR_PORTRAIT

// Ask the Android Activity to change its requested orientation.
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

bool remoteKeyForScancode(SDL_Scancode scancode, mctl::KeyCode& key) {
    switch (scancode) {
        case SDL_SCANCODE_BACKSPACE:
            key = mctl::KeyCode::Backspace;
            return true;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER:
            key = mctl::KeyCode::Enter;
            return true;
        case SDL_SCANCODE_TAB:
            key = mctl::KeyCode::Tab;
            return true;
        case SDL_SCANCODE_ESCAPE:
            key = mctl::KeyCode::Escape;
            return true;
        case SDL_SCANCODE_UP:
            key = mctl::KeyCode::Up;
            return true;
        case SDL_SCANCODE_DOWN:
            key = mctl::KeyCode::Down;
            return true;
        case SDL_SCANCODE_LEFT:
            key = mctl::KeyCode::Left;
            return true;
        case SDL_SCANCODE_RIGHT:
            key = mctl::KeyCode::Right;
            return true;
        default:
            return false;
    }
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
    OpenTouchpad,
    OpenKeyboard,
    ShowKeyboard,
    SendKey,
    BackToMenu,
    OpenServerShare,
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
    Tile,
};

// Icon drawn inside a square Look::Tile button.
enum class Icon {
    None,
    Touchpad,
    Keyboard,
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
    Icon icon = Icon::None;
    bool enabled = true;
};

enum class Screen {
    Discover,
    Manual,
    Pin,
    Menu,
    Pad,
    Keyboard,
    Share,
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
    void layoutMenu();
    void layoutPad();
    void layoutKeyboard();
    void layoutShare();
    void layoutKeypad(float top, float bottom, Action digitAction,
                      Action backAction, bool includeDot);

    void applyRotationMode();
    std::string rotationLabel() const;

    void render();
    void renderDiscover();
    void renderManual();
    void renderPin();
    void renderMenu();
    void renderPad();
    void renderKeyboard();
    void renderShare();
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
    void showMenu();
    void beginConnect(const std::string& ip, uint16_t port);
    void openKeyboard();
    void startKeyboardInput();
    void stopKeyboardInput();

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
    Screen orientationScreen_ = Screen::Discover;
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
    ServerShare share_;
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
    Rect keyboardInputRect_;
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

    std::vector<std::string> shareUrls_;
    uint64_t shareUrlsUpdatedMs_ = 0;
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool App::init() {
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    // SDL reads this only when the window is created. The live orientation is
    // controlled through JNI in applyRotationMode().
    SDL_SetHint(SDL_HINT_ORIENTATIONS,
                "LandscapeLeft,LandscapeRight,Portrait");
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
    stopKeyboardInput();
    share_.stop();
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

    if (screen_ == Screen::Share &&
        nowMs() - shareUrlsUpdatedMs_ >= 2000) {
        shareUrls_ = share_.urls();
        shareUrlsUpdatedMs_ = nowMs();
    }

    // A network event or a button may have changed the screen.
    updateLayout();
    render();
}

// ---------------------------------------------------------------------------
// Navigation / network
// ---------------------------------------------------------------------------

void App::showDiscover(const std::string& status, bool isError) {
    stopKeyboardInput();
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

void App::showMenu() {
    stopKeyboardInput();
    screen_ = Screen::Menu;
    hasPointer_ = false;
    hasPressedButton_ = false;

    pad_.cancel();
    pad_.takeEvents();
    moveAccX_ = 0.0f;
    moveAccY_ = 0.0f;
}

void App::beginConnect(const std::string& ip, uint16_t port) {
    stopKeyboardInput();
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

void App::openKeyboard() {
    screen_ = Screen::Keyboard;
    hasPointer_ = false;
    hasPressedButton_ = false;
    updateLayout();
    startKeyboardInput();
}

void App::startKeyboardInput() {
    if (screen_ == Screen::Keyboard && window_ != nullptr) {
        SDL_StartTextInput();
    }
}

void App::stopKeyboardInput() {
    net_.resetTextInput();
    if (window_ != nullptr && SDL_IsTextInputActive()) {
        SDL_StopTextInput();
    }
}

bool App::goBack() {
    switch (screen_) {
        case Screen::Discover:
            return false;
        case Screen::Manual:
        case Screen::Share:
            showDiscover();
            return true;
        case Screen::Menu:
            net_.disconnect();
            showDiscover("DISCONNECTED");
            return true;
        case Screen::Pin:
            net_.disconnect();
            showDiscover();
            return true;
        case Screen::Pad:
        case Screen::Keyboard:
            // Going back keeps the connection; the menu is the hub.
            showMenu();
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
                showMenu();
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
        case Screen::Menu:
            layoutMenu();
            break;
        case Screen::Pad:
            layoutPad();
            break;
        case Screen::Keyboard:
            layoutKeyboard();
            break;
        case Screen::Share:
            layoutShare();
            break;
    }

    // Orientation follows the screen: everything is sensor-driven except the
    // touchpad, which uses the mode chosen with the AUTO / PORT / LAND button.
    if (screen_ != orientationScreen_) {
        orientationScreen_ = screen_;
        applyRotationMode();
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

    const float buttonWidth = (width_ - 2.0f * m - gap) * 0.5f;

    Button setup;
    setup.rect = Rect{m, manualY, buttonWidth, manualHeight};
    setup.action = Action::OpenServerShare;
    setup.label = "PC SETUP";
    setup.textScale = fsBody_;
    setup.look = Look::Ghost;
    buttons_.push_back(setup);

    Button manual;
    manual.rect = Rect{m + buttonWidth + gap, manualY, buttonWidth,
                       manualHeight};
    manual.action = Action::OpenManual;
    manual.label = "MANUAL IP";
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

void App::layoutMenu() {
    const float m = 18.0f * u_;

    Button close;
    close.rect = Rect{width_ - m - 44.0f * u_, 12.0f * u_, 44.0f * u_,
                      44.0f * u_};
    close.action = Action::Disconnect;
    close.label = "X";
    close.textScale = fsBody_ + 1;
    close.look = Look::Ghost;
    buttons_.push_back(close);

    // Square tiles in a two-column grid, centred on screen. The tile size is
    // clamped by the available height so landscape does not overflow.
    const float gap = 14.0f * u_;
    const float gridTop = 92.0f * u_;
    const float gridBottom = height_ - m;
    const float columnWidth = (width_ - 2.0f * m - gap) * 0.5f;
    const float tileSize =
        std::min(columnWidth, std::max(0.0f, gridBottom - gridTop));
    const float gridWidth = tileSize * 2.0f + gap;
    const float gridX = (width_ - gridWidth) * 0.5f;

    Button touchpad;
    touchpad.rect = Rect{gridX, gridTop, tileSize, tileSize};
    touchpad.action = Action::OpenTouchpad;
    touchpad.label = "TOUCHPAD";
    touchpad.sub = "MOVE + CLICK";
    touchpad.textScale = fsBody_;
    touchpad.look = Look::Tile;
    touchpad.icon = Icon::Touchpad;
    buttons_.push_back(touchpad);

    Button keyboard;
    keyboard.rect = Rect{gridX + tileSize + gap, gridTop, tileSize, tileSize};
    keyboard.action = Action::OpenKeyboard;
    keyboard.label = "KEYBOARD";
    keyboard.sub = "TYPE + KEYS";
    keyboard.textScale = fsBody_;
    keyboard.look = Look::Tile;
    keyboard.icon = Icon::Keyboard;
    buttons_.push_back(keyboard);
}

void App::layoutPad() {
    const float m = 14.0f * u_;

    Button back;
    back.rect = Rect{m, 12.0f * u_, 44.0f * u_, 44.0f * u_};
    back.action = Action::BackToMenu;
    back.label = "<";
    back.textScale = fsTitle_;
    back.look = Look::Ghost;
    buttons_.push_back(back);

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

void App::layoutKeyboard() {
    const float m = 14.0f * u_;

    Button back;
    back.rect = Rect{m, 12.0f * u_, 44.0f * u_, 44.0f * u_};
    back.action = Action::BackToMenu;
    back.label = "<";
    back.textScale = fsTitle_;
    back.look = Look::Ghost;
    buttons_.push_back(back);

    Button close;
    close.rect = Rect{width_ - m - 44.0f * u_, 12.0f * u_, 44.0f * u_,
                      44.0f * u_};
    close.action = Action::Disconnect;
    close.label = "X";
    close.textScale = fsBody_ + 1;
    close.look = Look::Ghost;
    buttons_.push_back(close);

    const bool landscape = width_ > height_;
    const float gap = 8.0f * u_;
    const float fullWidth = width_ - 2.0f * m;
    const float inputWidth = landscape ? fullWidth * 0.25f : fullWidth;
    const float inputHeight = (landscape ? 52.0f : 62.0f) * u_;
    keyboardInputRect_ =
        Rect{m, (landscape ? 68.0f : 74.0f) * u_, inputWidth, inputHeight};
    Button input;
    input.rect = keyboardInputRect_;
    input.action = Action::ShowKeyboard;
    input.label = "TAP HERE TO TYPE";
    input.textScale = landscape ? fsSmall_ : fsBody_;
    input.look = Look::Secondary;
    buttons_.push_back(input);

    // Landscape keeps every special key beside the IME target, above the area
    // covered by the phone keyboard. Portrait uses two larger rows below it.
    const int columns = landscape ? 8 : 4;
    const float gridX =
        landscape ? keyboardInputRect_.x + keyboardInputRect_.w + gap : m;
    const float gridY =
        landscape ? keyboardInputRect_.y
                  : keyboardInputRect_.y + keyboardInputRect_.h + 12.0f * u_;
    const float gridWidth = landscape ? width_ - m - gridX : fullWidth;
    const float keyWidth =
        (gridWidth - static_cast<float>(columns - 1) * gap) /
        static_cast<float>(columns);
    const float keyHeight = inputHeight;

    static const char* const labels[8] = {
        "BACKSPACE", "ENTER", "TAB", "ESC", "LEFT", "DOWN", "UP", "RIGHT","SPACE","DELETE"
    };
    static const mctl::KeyCode keys[8] = {
        mctl::KeyCode::Backspace, mctl::KeyCode::Enter,
        mctl::KeyCode::Up,        mctl::KeyCode::Tab,       
        mctl::KeyCode::Escape,
        mctl::KeyCode::Left,      mctl::KeyCode::Down,
        mctl::KeyCode::Right,
        mctl::KeyCode::Space,     mctl::KeyCode::Delete
    };

    for (int i = 0; i < 8; ++i) {
        const int column = i % columns;
        const int row = i / columns;
        Button key;
        key.rect = Rect{gridX + static_cast<float>(column) * (keyWidth + gap),
                        gridY + static_cast<float>(row) * (keyHeight + gap),
                        keyWidth, keyHeight};
        key.action = Action::SendKey;
        key.value = static_cast<int>(keys[i]);
        key.label = labels[i];
        key.textScale = i == 0 ? fsTiny_ : fsSmall_;
        key.look = Look::Secondary;
        buttons_.push_back(key);
    }

    const SDL_Rect inputRect{
        static_cast<int>(std::lround(keyboardInputRect_.x)),
        static_cast<int>(std::lround(keyboardInputRect_.y)),
        static_cast<int>(std::lround(keyboardInputRect_.w)),
        static_cast<int>(std::lround(keyboardInputRect_.h))};
    SDL_SetTextInputRect(&inputRect);
}

void App::layoutShare() {
    const float m = 18.0f * u_;

    Button back;
    back.rect = Rect{m, 18.0f * u_, 46.0f * u_, 46.0f * u_};
    back.action = Action::GoBack;
    back.label = "<";
    back.textScale = fsTitle_;
    back.look = Look::Ghost;
    buttons_.push_back(back);

    if (!share_.running()) {
        Button retry;
        retry.rect = Rect{m, height_ - m - 50.0f * u_, width_ - 2.0f * m,
                          50.0f * u_};
        retry.action = Action::OpenServerShare;
        retry.label = "RETRY";
        retry.textScale = fsBody_;
        retry.look = Look::Primary;
        buttons_.push_back(retry);
    }
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
    // Every screen follows the device sensor. Only the touchpad honours the
    // AUTO / PORT / LAND choice from the pad header.
    int orientation = kOrientSensor;
    if (screen_ == Screen::Pad) {
        switch (rotationMode_) {
            case RotationMode::Auto:
                orientation = kOrientSensor;
                break;
            case RotationMode::Portrait:
                orientation = kOrientSensorPortrait;
                break;
            case RotationMode::Landscape:
                orientation = kOrientSensorLandscape;
                break;
        }
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
            rotationMode_ =
                static_cast<RotationMode>((static_cast<int>(rotationMode_) + 1) %
                                          3);
            applyRotationMode();
            break;
        }

        case Action::OpenTouchpad:
            screen_ = Screen::Pad;
            hasPointer_ = false;
            hasPressedButton_ = false;
            pad_.cancel();
            pad_.takeEvents();
            moveAccX_ = 0.0f;
            moveAccY_ = 0.0f;
            break;

        case Action::OpenKeyboard:
            openKeyboard();
            break;

        case Action::ShowKeyboard:
            startKeyboardInput();
            break;

        case Action::SendKey:
            net_.sendKey(static_cast<mctl::KeyCode>(button.value));
            break;

        case Action::BackToMenu:
            showMenu();
            break;

        case Action::OpenServerShare:
            if (!share_.running()) share_.start();
            shareUrls_ = share_.urls();
            shareUrlsUpdatedMs_ = nowMs();
            screen_ = Screen::Share;
            hasPointer_ = false;
            hasPressedButton_ = false;
            break;

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
            stopKeyboardInput();
            // Never leave a drag latched on the PC while we are suspended.
            pad_.cancel();
            flushPad();
            // A native thread is not a foreground service, so do not advertise
            // downloads while Android may suspend or kill this process.
            share_.stop();
            shareUrls_.clear();
            break;

        case SDL_APP_DIDENTERFOREGROUND:
            net_.resume();
            if (screen_ == Screen::Keyboard) startKeyboardInput();
            if (screen_ == Screen::Share) {
                share_.start();
                shareUrls_ = share_.urls();
                shareUrlsUpdatedMs_ = nowMs();
            }
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

        case SDL_KEYDOWN: {
            const SDL_Scancode scancode = event.key.keysym.scancode;
            if (scancode == SDL_SCANCODE_AC_BACK) {
                if (!event.key.repeat && !goBack()) running_ = false;
                break;
            }

            if (screen_ == Screen::Keyboard) {
                mctl::KeyCode key = mctl::KeyCode::Backspace;
                if (remoteKeyForScancode(scancode, key)) {
                    net_.sendKey(key);
                    break;
                }
            }

            if (!event.key.repeat && scancode == SDL_SCANCODE_ESCAPE) {
                if (!goBack()) running_ = false;
            }
            break;
        }

        case SDL_TEXTINPUT:
            if (screen_ == Screen::Keyboard && net_.ready()) {
                // SDL also emits key events for ASCII control characters. Send
                // those through KEYDOWN only, avoiding duplicate Enter/Tab input.
                std::string text;
                for (const unsigned char c : std::string(event.text.text)) {
                    if (c >= 0x20u && c != 0x7Fu) {
                        text.push_back(static_cast<char>(c));
                    }
                }
                net_.sendText(text);
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
        case Screen::Menu:
            renderMenu();
            break;
        case Screen::Pad:
            renderPad();
            break;
        case Screen::Keyboard:
            renderKeyboard();
            break;
        case Screen::Share:
            renderShare();
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
                             : "USE MANUAL IP BELOW",
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

void App::renderMenu() {
    const float m = 18.0f * u_;
    const std::string name =
        net_.serverName().empty() ? "REMOTE PC" : net_.serverName();

    const float dot = 7.0f * u_;
    const float statusY = 24.0f * u_;
    ui::fillRoundedRect(
        renderer_,
        Rect{m, statusY + (ui::textHeight(fsTiny_) - dot) * 0.5f, dot, dot},
        dot * 0.5f, ui::colors::success);
    ui::text(renderer_, m + dot + 6.0f * u_, statusY, "CONNECTED", fsTiny_,
             ui::colors::success);

    const float nameY = statusY + ui::textHeight(fsTiny_) + 8.0f * u_;
    ui::text(renderer_, m, nameY,
             fitText(name, width_ - 2.0f * m - 52.0f * u_, fsTitle_), fsTitle_,
             ui::colors::text);

    ui::text(renderer_, m,
             nameY + ui::textHeight(fsTitle_) + 8.0f * u_, "CHOOSE A CONTROL",
             fsTiny_, ui::colors::textMuted);
}

void App::renderPad() {
    const float m = 14.0f * u_;
    const float nameX = m + 52.0f * u_;

    const std::string name =
        net_.serverName().empty() ? "REMOTE PC" : net_.serverName();
    ui::text(renderer_, nameX, 15.0f * u_,
             fitText(name, width_ - nameX - m - 104.0f * u_, fsBody_), fsBody_,
             ui::colors::text);

    const float subY = 15.0f * u_ + ui::textHeight(fsBody_) + 5.0f * u_;
    const float dot = 7.0f * u_;
    ui::fillRoundedRect(
        renderer_,
        Rect{nameX, subY + (ui::textHeight(fsTiny_) - dot) * 0.5f, dot, dot},
        dot * 0.5f, ui::colors::success);
    ui::text(renderer_, nameX + dot + 6.0f * u_, subY, "PAIRED", fsTiny_,
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

void App::renderKeyboard() {
    const float m = 14.0f * u_;
    const float nameX = m + 52.0f * u_;
    const std::string name =
        net_.serverName().empty() ? "REMOTE PC" : net_.serverName();

    ui::text(renderer_, nameX, 15.0f * u_,
             fitText(name, width_ - nameX - m - 52.0f * u_, fsBody_), fsBody_,
             ui::colors::text);

    const float subY = 15.0f * u_ + ui::textHeight(fsBody_) + 5.0f * u_;
    const float dot = 7.0f * u_;
    ui::fillRoundedRect(
        renderer_,
        Rect{nameX, subY + (ui::textHeight(fsTiny_) - dot) * 0.5f, dot, dot},
        dot * 0.5f, ui::colors::success);
    ui::text(renderer_, nameX + dot + 6.0f * u_, subY, "KEYBOARD PAIRED",
             fsTiny_, ui::colors::success);

    ui::textCentered(
        renderer_,
        Rect{m, 58.0f * u_, width_ - 2.0f * m, ui::textHeight(fsTiny_)},
        fitText("TEXT GOES DIRECTLY TO YOUR PC", width_ - 2.0f * m, fsTiny_),
        fsTiny_, ui::colors::textMuted);
}

void App::renderShare() {
    const float m = 18.0f * u_;

    ui::text(renderer_, m + 60.0f * u_,
             18.0f * u_ + (46.0f * u_ - ui::textHeight(fsBody_)) * 0.5f,
             "PC SERVER", fsBody_, ui::colors::text);

    const bool ready = share_.running();
    const float statusY = 18.0f * u_ + 46.0f * u_ + 20.0f * u_;
    const float dot = 7.0f * u_;
    const SDL_Color statusColor =
        ready ? ui::colors::success : ui::colors::danger;
    ui::fillRoundedRect(
        renderer_,
        Rect{m, statusY + (ui::textHeight(fsTiny_) - dot) * 0.5f, dot, dot},
        dot * 0.5f, statusColor);
    const std::string status =
        ready ? std::string("READY - NO INTERNET NEEDED")
              : (share_.error().empty() ? std::string("DOWNLOAD SERVER STOPPED")
                                        : share_.error());
    ui::text(renderer_, m + dot + 6.0f * u_, statusY,
             fitText(status, width_ - 2.0f * m - dot - 6.0f * u_, fsTiny_),
             fsTiny_, statusColor);

    const float cardY = statusY + ui::textHeight(fsTiny_) + 10.0f * u_;
    const Rect card{m, cardY, width_ - 2.0f * m, 64.0f * u_};
    ui::fillRoundedRect(renderer_, card, 14.0f * u_,
                        ready ? ui::colors::accentDim : ui::colors::border);
    ui::fillRoundedRect(renderer_, inset(card, 2.0f * u_), 12.0f * u_,
                        ui::colors::surface);

    ui::text(renderer_, card.x + 12.0f * u_, card.y + 9.0f * u_,
             ready ? "OPEN THIS ADDRESS ON THE PC" : "SETUP UNAVAILABLE",
             fsTiny_, ui::colors::textMuted);

    const std::string url =
        !ready ? std::string("TAP RETRY BELOW")
               : (shareUrls_.empty()
                      ? std::string("NO LOCAL WIFI ADDRESS FOUND")
                      : shareUrls_.front());
    int urlScale = fsBody_;
    const float urlLimit = card.w - 24.0f * u_;
    if (ui::textWidth(url, urlScale) > urlLimit) urlScale = fsSmall_;
    if (ui::textWidth(url, urlScale) > urlLimit) urlScale = fsTiny_;
    ui::textCentered(
        renderer_,
        Rect{card.x + 12.0f * u_, card.y + 28.0f * u_, card.w - 24.0f * u_,
             26.0f * u_},
        fitText(url, urlLimit, urlScale), urlScale,
        ready ? ui::colors::accent : ui::colors::textMuted);

    if (!ready) return;

    static const char* const steps[] = {
        "1  CONNECT THE PC TO THE SAME WIFI OR HOTSPOT",
        "2  OPEN A BROWSER ON THE PC",
        "3  TYPE THE ADDRESS ABOVE",
        "4  DOWNLOAD AND OPEN THE SERVER",
        "5  ALLOW NETWORK ACCESS IF WINDOWS ASKS",
    };

    const float footerY = height_ - m - ui::textHeight(fsTiny_);
    const float stepsBottom = footerY - ui::textHeight(fsTiny_) - 12.0f * u_;
    float y = card.y + card.h + 14.0f * u_;
    float gap = 9.0f * u_;
    const float needed = 5.0f * ui::textHeight(fsSmall_) + 4.0f * gap;
    if (y + needed > stepsBottom) gap = 3.0f * u_;

    for (const char* step : steps) {
        if (y + ui::textHeight(fsSmall_) > stepsBottom) break;
        ui::text(renderer_, m, y, fitText(step, width_ - 2.0f * m, fsSmall_),
                 fsSmall_, ui::colors::text);
        y += ui::textHeight(fsSmall_) + gap;
    }

    char sizeText[64];
    std::snprintf(sizeText, sizeof(sizeText), "%.2f MB",
                  static_cast<double>(share_.payloadSize()) /
                      (1024.0 * 1024.0));
    const std::string fileText =
        std::string("REMOTE_MOUSE_SERVER.EXE - ") + sizeText;
    ui::text(renderer_, m, footerY - ui::textHeight(fsTiny_) - 7.0f * u_,
             fitText(fileText, width_ - 2.0f * m, fsTiny_), fsTiny_,
             ui::colors::textMuted);
    ui::text(renderer_, m, footerY,
             fitText("GO BACK IS OK; KEEP THE APP OPEN WHILE DOWNLOADING",
                     width_ - 2.0f * m, fsTiny_),
             fsTiny_, ui::colors::textMuted);
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
            case Look::Tile:
                fill = pressed ? ui::colors::surfaceRaised
                               : ui::colors::surface;
                borderColor =
                    pressed ? ui::colors::accent : ui::colors::border;
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
        } else if (button.look == Look::Tile) {
            const SDL_Color iconColor =
                button.enabled ? ui::colors::accent : ui::colors::textMuted;
            const float iconSize =
                std::min(button.rect.w * 0.34f, button.rect.h * 0.28f);
            const float iconX = button.rect.centerX() - iconSize * 0.5f;
            const float iconY = button.rect.y + button.rect.h * 0.20f;

            if (button.icon == Icon::Touchpad) {
                const float padH = iconSize * 0.74f;
                const float iconRadius = iconSize * 0.22f;
                const Rect padRect{iconX, iconY, iconSize, padH};
                ui::fillRoundedRect(renderer_, padRect, iconRadius, iconColor);
                ui::fillRoundedRect(renderer_, inset(padRect, 2.0f * u_),
                                    std::max(0.0f, iconRadius - 2.0f * u_),
                                    fill);

                const float cursor = std::max(3.0f * u_, iconSize * 0.16f);
                ui::fillRoundedRect(
                    renderer_,
                    Rect{padRect.centerX() - cursor * 0.5f,
                         padRect.centerY() - cursor * 0.5f, cursor, cursor},
                    cursor * 0.5f, iconColor);
            } else if (button.icon == Icon::Keyboard) {
                const float keyboardH = iconSize * 0.68f;
                const Rect keyboardRect{iconX, iconY, iconSize, keyboardH};
                const float iconRadius = iconSize * 0.12f;
                ui::fillRoundedRect(renderer_, keyboardRect, iconRadius,
                                    iconColor);
                ui::fillRoundedRect(renderer_,
                                    inset(keyboardRect, 2.0f * u_),
                                    std::max(0.0f, iconRadius - 2.0f * u_), fill);

                const float keyGap = iconSize * 0.055f;
                const float keySize = iconSize * 0.12f;
                for (int row = 0; row < 2; ++row) {
                    for (int column = 0; column < 4; ++column) {
                        ui::fillRoundedRect(
                            renderer_,
                            Rect{iconX + iconSize * 0.12f +
                                     static_cast<float>(column) *
                                         (keySize + keyGap),
                                 iconY + iconSize * 0.13f +
                                     static_cast<float>(row) *
                                         (keySize + keyGap),
                                 keySize, keySize},
                            keySize * 0.18f, iconColor);
                    }
                }
            }

            const float labelY = button.rect.y + button.rect.h * 0.66f;
            const float textLimit = button.rect.w - 16.0f * u_;
            ui::textCentered(
                renderer_,
                Rect{button.rect.x, labelY, button.rect.w,
                     ui::textHeight(button.textScale)},
                fitText(button.label, textLimit, button.textScale),
                button.textScale, textColor);

            if (!button.sub.empty()) {
                ui::textCentered(
                    renderer_,
                    Rect{button.rect.x,
                         labelY + ui::textHeight(button.textScale) +
                             5.0f * u_,
                         button.rect.w, ui::textHeight(fsTiny_)},
                    fitText(button.sub, textLimit, fsTiny_), fsTiny_,
                    ui::colors::textMuted);
            }
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
