#include "etc2.hpp"
#include "../../extern/rg_etc1/rg_etc1.h"
#define XXH_INLINE_ALL
#include <xxhash.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>

namespace aurora::gfx {
namespace {
size_t rgba_size(uint32_t width, uint32_t height, uint32_t mips) {
  size_t size = 0;
  for (uint32_t i = 0; i < mips; ++i) {
    size += size_t(std::max(1u, width >> i)) * std::max(1u, height >> i) * 4;
  }
  return size;
}
size_t encoded_size(uint32_t width, uint32_t height, uint32_t mips, bool alpha) {
  size_t size = 0;
  for (uint32_t i = 0; i < mips; ++i) {
    size += size_t((std::max(1u, width >> i) + 3) / 4) * ((std::max(1u, height >> i) + 3) / 4) * (alpha ? 16 : 8);
  }
  return size;
}
}

bool etc2_eligible(uint32_t width, uint32_t height, uint32_t mips) noexcept {
  if (width < 32 || height < 32 || width > 16384 || height > 16384 || width % 4 || height % 4 || !mips) return false;
  uint32_t maximum = 1;
  for (uint32_t size = std::max(width, height); size > 1; size >>= 1) ++maximum;
  return mips <= maximum;
}

Etc2Texture encode_etc2(uint32_t width, uint32_t height, uint32_t mips, std::span<const uint8_t> rgba) {
  if (!etc2_eligible(width, height, mips) || rgba.size() != rgba_size(width, height, mips)) return {};
  bool alpha = false;
  for (size_t i = 3; i < rgba.size(); i += 4) {
    if (rgba[i] != 0 && rgba[i] != 255) return {};
    alpha |= rgba[i] == 0;
  }
  static std::once_flag initialized;
  std::call_once(initialized, rg_etc1::pack_etc1_block_init);
  Etc2Texture result{std::vector<uint8_t>(encoded_size(width, height, mips, alpha)), alpha};
  rg_etc1::etc1_pack_params params;
  params.m_quality = rg_etc1::cLowQuality;
  size_t sourceOffset = 0, outputOffset = 0;
  for (uint32_t mip = 0; mip < mips; ++mip) {
    const uint32_t w = std::max(1u, width >> mip), h = std::max(1u, height >> mip);
    for (uint32_t by = 0; by < h; by += 4) {
      for (uint32_t bx = 0; bx < w; bx += 4) {
        std::array<uint32_t, 16> block{};
        uint64_t selectors = 0;
        for (uint32_t y = 0; y < 4; ++y) {
          for (uint32_t x = 0; x < 4; ++x) {
            const auto* pixel = rgba.data() + sourceOffset + (size_t(std::min(by + y, h - 1)) * w + std::min(bx + x, w - 1)) * 4;
            auto* target = reinterpret_cast<uint8_t*>(&block[y * 4 + x]);
            std::memcpy(target, pixel, 3);
            target[3] = 255;
            selectors |= uint64_t(pixel[3] ? 7 : 3) << (45 - (x * 4 + y) * 3);
          }
        }
        auto* destination = result.data.data() + outputOffset;
        if (alpha) {
          destination[0] = 128;
          destination[1] = 0xf0;
          for (int i = 0; i < 6; ++i) destination[2 + i] = uint8_t(selectors >> (40 - i * 8));
          destination += 8;
        }
        rg_etc1::pack_etc1_block(destination, block.data(), params);
        outputOffset += alpha ? 16 : 8;
      }
    }
    sourceOffset += size_t(w) * h * 4;
  }
  return result;
}

Etc2Texture cached_etc2(uint32_t width, uint32_t height, uint32_t mips, std::span<const uint8_t> rgba,
                       const std::filesystem::path& directory) {
  if (!etc2_eligible(width, height, mips) || rgba.size() != rgba_size(width, height, mips)) return {};
  const auto hash = XXH3_128bits(rgba.data(), rgba.size());
  char name[128];
  std::snprintf(name, sizeof(name), "%016llx%016llx-%u-%u-%u.etc2", static_cast<unsigned long long>(hash.high64),
                static_cast<unsigned long long>(hash.low64), width, height, mips);
  const auto path = directory / name;
  if (!directory.empty()) {
    std::ifstream file(path, std::ios::binary);
    std::array<uint64_t, 4> header{};
    if (file.read(reinterpret_cast<char*>(header.data()), sizeof(header)) && header[0] == 0x4554433200000001ull && header[1] <= 1 &&
        header[2] == encoded_size(width, height, mips, header[1] != 0)) {
      Etc2Texture result{std::vector<uint8_t>(header[2]), header[1] != 0};
      if (file.read(reinterpret_cast<char*>(result.data.data()), result.data.size()) && file.peek() == EOF &&
          XXH3_64bits(result.data.data(), result.data.size()) == header[3]) return result;
    }
  }
  auto result = encode_etc2(width, height, mips, rgba);
  if (!directory.empty() && !result.data.empty()) {
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (!error) {
      const auto temporary = path.string() + ".tmp";
      std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
      const std::array<uint64_t, 4> header{0x4554433200000001ull, result.alpha ? 1ull : 0ull, uint64_t(result.data.size()),
                                           XXH3_64bits(result.data.data(), result.data.size())};
      file.write(reinterpret_cast<const char*>(header.data()), sizeof(header));
      file.write(reinterpret_cast<const char*>(result.data.data()), result.data.size());
      file.close();
      if (file) std::filesystem::rename(temporary, path, error);
      if (error || !file) std::filesystem::remove(temporary, error);
    }
  }
  return result;
}
}
