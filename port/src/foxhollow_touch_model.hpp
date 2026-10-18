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
