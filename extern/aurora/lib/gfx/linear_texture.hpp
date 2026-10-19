#pragma once

#include <cstdint>

namespace aurora::gfx {
inline constexpr uint32_t LinearTextureWidth = 256;
inline constexpr uint32_t LinearTextureBytesPerTexel = 16;
inline constexpr uint32_t LinearTextureBytesPerRow = LinearTextureWidth * LinearTextureBytesPerTexel;

constexpr bool needs_texture_vertex_fetch(uint32_t vertexStorageBuffers, uint32_t fragmentStorageBuffers,
                                          bool compatibilityProfile = false) {
  return compatibilityProfile || vertexStorageBuffers < 2 || fragmentStorageBuffers < 1;
}

struct LinearTextureCopyRange {
  uint32_t offset = 0;
  uint32_t y = 0;
  uint32_t rows = 0;
};

constexpr LinearTextureCopyRange linear_texture_copy_range(uint32_t copied, uint32_t highWater) {
  if (highWater <= copied) {
    return {};
  }
  const uint32_t firstRow = copied / LinearTextureBytesPerRow;
  const uint32_t endRow = (highWater + LinearTextureBytesPerRow - 1) / LinearTextureBytesPerRow;
  return {firstRow * LinearTextureBytesPerRow, firstRow, endRow - firstRow};
}
}
