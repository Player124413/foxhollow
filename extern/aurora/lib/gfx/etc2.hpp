#pragma once
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace aurora::gfx {
struct Etc2Texture {
  std::vector<uint8_t> data;
  bool alpha = false;
};
bool etc2_eligible(uint32_t width, uint32_t height, uint32_t mips) noexcept;
Etc2Texture encode_etc2(uint32_t width, uint32_t height, uint32_t mips, std::span<const uint8_t> rgba);
Etc2Texture cached_etc2(uint32_t width, uint32_t height, uint32_t mips, std::span<const uint8_t> rgba,
                       const std::filesystem::path& directory);
}
