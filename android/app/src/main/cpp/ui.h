#pragma once

#include <SDL.h>

#include <string>

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    bool contains(float px, float py) const {
        return px >= x && py >= y && px < x + w && py < y + h;
    }

    float centerX() const { return x + w * 0.5f; }
    float centerY() const { return y + h * 0.5f; }
};

namespace ui {

namespace colors {
constexpr SDL_Color background{11, 15, 20, 255};
constexpr SDL_Color surface{17, 24, 39, 255};
constexpr SDL_Color surfaceRaised{30, 41, 59, 255};
constexpr SDL_Color border{51, 65, 85, 255};
constexpr SDL_Color accent{34, 211, 238, 255};
constexpr SDL_Color accentDim{14, 116, 144, 255};
constexpr SDL_Color text{229, 231, 235, 255};
constexpr SDL_Color textMuted{148, 163, 184, 255};
constexpr SDL_Color danger{248, 113, 113, 255};
constexpr SDL_Color success{52, 211, 153, 255};
}  // namespace colors

void fillRect(SDL_Renderer* renderer, const Rect& rect, SDL_Color color);
void fillRoundedRect(SDL_Renderer* renderer, const Rect& rect, float radius,
                     SDL_Color color);
void strokeRect(SDL_Renderer* renderer, const Rect& rect, float thickness,
                SDL_Color color);

// Bitmap text: pixelScale is the size in pixels of one font pixel.
void text(SDL_Renderer* renderer, float x, float y, const std::string& value,
          int pixelScale, SDL_Color color);
float textWidth(const std::string& value, int pixelScale);
float textHeight(int pixelScale);
void textCentered(SDL_Renderer* renderer, const Rect& rect,
                  const std::string& value, int pixelScale, SDL_Color color);

}  // namespace ui
