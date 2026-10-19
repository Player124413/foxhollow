#include "../extern/aurora/lib/gfx/etc2.hpp"
#include "../extern/aurora/extern/rg_etc1/rg_etc1.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace aurora::gfx;
namespace {
std::vector<uint8_t> image(uint32_t w, uint32_t h, uint32_t mips, bool alpha) {
  std::vector<uint8_t> pixels;
  for (uint32_t mip = 0; mip < mips; ++mip) {
    for (uint32_t y = 0; y < std::max(1u, h >> mip); ++y) {
      for (uint32_t x = 0; x < std::max(1u, w >> mip); ++x) {
        pixels.insert(pixels.end(), {uint8_t(80 + mip * 10), uint8_t(140), uint8_t(200), uint8_t(alpha && (x + y) % 3 == 0 ? 0 : 255)});
      }
    }
  }
  return pixels;
}
void roundtrip(bool alpha) {
  constexpr uint32_t width = 32, height = 64, mips = 7;
  const auto pixels = image(width, height, mips, alpha);
  const auto encoded = encode_etc2(width, height, mips, pixels);
  assert(encoded.alpha == alpha && !encoded.data.empty());
  size_t offset = 0, sourceOffset = 0;
  for (uint32_t mip = 0; mip < mips; ++mip) {
    const uint32_t w = std::max(1u, width >> mip), h = std::max(1u, height >> mip);
    for (uint32_t by = 0; by < h; by += 4) {
      for (uint32_t bx = 0; bx < w; bx += 4) {
        const auto* block = encoded.data.data() + offset;
        std::array<unsigned int, 16> rgb{};
        assert(rg_etc1::unpack_etc1_block(block + (alpha ? 8 : 0), rgb.data()));
        uint64_t bits = 0;
        if (alpha) for (int i = 0; i < 6; ++i) bits = (bits << 8) | block[i + 2];
        for (uint32_t y = 0; y < 4; ++y) for (uint32_t x = 0; x < 4; ++x) {
          const auto* source = pixels.data() + sourceOffset + (size_t(std::min(by + y, h - 1)) * w + std::min(bx + x, w - 1)) * 4;
          const auto* color = reinterpret_cast<const uint8_t*>(&rgb[y * 4 + x]);
          for (int c = 0; c < 3; ++c) assert(std::abs(int(color[c]) - source[c]) <= 15);
          if (alpha) {
            constexpr std::array<int, 8> table{-3, -6, -9, -15, 2, 5, 8, 14};
            const auto selector = (bits >> (45 - (x * 4 + y) * 3)) & 7;
            assert((block[1] & 15) == 0);
            int decoded = std::clamp(int(block[0]) + (block[1] >> 4) * table[selector], 0, 255);
            assert(decoded == source[3]);
          }
        }
        offset += alpha ? 16 : 8;
      }
    }
    sourceOffset += size_t(w) * h * 4;
  }
  assert(offset == encoded.data.size());
  assert(sourceOffset == pixels.size());
}
}
int main() {
  assert(!etc2_eligible(0, 32, 1));
  assert(!etc2_eligible(16, 16, 1));
  assert(!etc2_eligible(33, 32, 1));
  assert(!etc2_eligible(32, 32, 7));
  assert(!etc2_eligible(32, 32, 32));
  assert(!etc2_eligible(65536, 32, 1));
  assert(etc2_eligible(36, 32, 6));
  std::thread one([] { roundtrip(false); });
  std::thread two([] { roundtrip(true); });
  one.join(); two.join();
  auto pixels = image(32, 32, 1, false);
  assert(encode_etc2(32, 32, 1, pixels).data.size() == pixels.size() / 8);
  auto alpha = image(32, 32, 1, true);
  assert(encode_etc2(32, 32, 1, alpha).data.size() == alpha.size() / 4);
  pixels.pop_back();
  assert(encode_etc2(32, 32, 1, pixels).data.empty());
  pixels = image(32, 32, 1, false);
  pixels[3] = 128;
  assert(encode_etc2(32, 32, 1, pixels).data.empty());
  pixels = image(32, 32, 1, false);
  char directory[] = "/tmp/foxhollow-etc2-XXXXXX";
  assert(mkdtemp(directory));
  auto first = cached_etc2(32, 32, 1, pixels, directory);
  auto second = cached_etc2(32, 32, 1, pixels, directory);
  assert(first.data == second.data && !first.data.empty());
  auto file = std::filesystem::directory_iterator(directory)->path();
  { std::ofstream corrupt(file, std::ios::binary | std::ios::trunc); corrupt << "bad"; }
  assert(cached_etc2(32, 32, 1, pixels, directory).data == first.data);
  { std::ofstream corrupt(file, std::ios::binary | std::ios::app); corrupt << 'X'; }
  assert(cached_etc2(32, 32, 1, pixels, directory).data == first.data);
  std::filesystem::remove_all(directory);
}
