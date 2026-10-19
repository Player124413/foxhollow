#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace fh::touch {
struct Control {
  const char* label;
  float x, y, radius;
  uint16_t button;
};
inline constexpr std::array<Control, 14> defaults{{
    {"Stick", .16f, .72f, .12f, 0}, {"C", .70f, .73f, .09f, 0},
    {"A", .89f, .67f, .065f, 0x100}, {"B", .80f, .81f, .055f, 0x200},
    {"X", .91f, .48f, .05f, 0x400}, {"Y", .80f, .49f, .05f, 0x800},
    {"L", .10f, .18f, .055f, 0x40}, {"R", .90f, .18f, .055f, 0x20},
    {"Z", .76f, .20f, .05f, 0x10}, {"Start", .50f, .12f, .045f, 0x1000},
    {"Up", .39f, .66f, .04f, 8}, {"Down", .39f, .86f, .04f, 4},
    {"Left", .34f, .76f, .04f, 1}, {"Right", .44f, .76f, .04f, 2},
}};
inline constexpr Control defaultMenu{"Menu", .55f, .93f, .05f, 0};
inline std::array<float, 2> menuSize(const Control& control, float width, float height) {
  const float unit = std::min(width, height);
  const float scale = std::clamp(unit / 480.f, .75f, 3.f);
  return {std::max(104.f * scale, 4.f * control.radius * unit),
          std::max(44.f * scale, 2.f * control.radius * unit)};
}
inline void constrainMenu(Control& control, float width, float height) {
  if (width <= 0 || height <= 0) return;
  auto extent = menuSize(control, width, height);
  const float rx = std::min(.5f, extent[0] / (2.f * width));
  const float ry = std::min(.5f, extent[1] / (2.f * height));
  control.x = std::clamp(control.x, rx, 1.f - rx);
  control.y = std::clamp(control.y, ry, 1.f - ry);
}
struct Thumb {
  float x = 0, y = 0;
  void follow(float tx, float ty, float dt) {
    if (!std::isfinite(tx) || !std::isfinite(ty)) tx = ty = 0;
    float length = std::max(1.f, std::hypot(tx, ty));
    tx /= length;
    ty /= length;
    const float blend = 1.f - std::exp(-24.f * std::clamp(dt, 0.f, .1f));
    x += (tx - x) * blend;
    y += (ty - y) * blend;
  }
};
inline bool valid(float x, float y, float r) {
  return std::isfinite(x) && std::isfinite(y) && std::isfinite(r) &&
         x >= 0 && x <= 1 && y >= 0 && y <= 1 && r >= .025f && r <= .20f;
}
inline void constrain(Control& control, float aspect) {
  if (!std::isfinite(aspect) || aspect <= 0.f) return;
  const float rx = control.radius * std::min(1.f, 1.f / aspect);
  const float ry = control.radius * std::min(1.f, aspect);
  control.x = std::clamp(control.x, rx, 1.f - rx);
  control.y = std::clamp(control.y, ry, 1.f - ry);
}
inline std::array<int8_t, 2> stick(float x, float y, float deadzone) {
  if (!std::isfinite(x) || !std::isfinite(y)) return {0, 0};
  const float length = std::hypot(x, y);
  deadzone = std::clamp(deadzone, 0.f, .9f);
  if (length <= deadzone) return {0, 0};
  const float scale = (std::min(length, 1.f) - deadzone) / ((1.f - deadzone) * length);
  return {static_cast<int8_t>(std::lround(x * scale * 100)),
          static_cast<int8_t>(std::lround(-y * scale * 100))};
}
}
