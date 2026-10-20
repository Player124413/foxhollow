#pragma once

#include <cstddef>
#include <cstdint>

namespace aurora::gfx {
constexpr uint32_t probe_row_pitch(uint32_t width) { return (width * 4u + 255u) & ~255u; }

struct FrameProbeStats {
  uint32_t nonblack = 0;
  uint32_t nonopaque = 0;
  uint32_t maxRgb = 0;
};

inline FrameProbeStats probe_rows(const uint8_t* bytes, uint32_t width, uint32_t pitch, uint32_t height = 3) {
  FrameProbeStats stats;
  for (uint32_t row = 0; row < height; ++row) {
    for (uint32_t x = 0; x < width; ++x) {
      const auto* pixel = bytes + static_cast<size_t>(row) * pitch + x * 4u;
      uint32_t peak = pixel[0];
      if (pixel[1] > peak) peak = pixel[1];
      if (pixel[2] > peak) peak = pixel[2];
      stats.nonblack += peak > 2u;
      stats.nonopaque += pixel[3] < 255u;
      if (peak > stats.maxRgb) stats.maxRgb = peak;
    }
  }
  return stats;
}
}
