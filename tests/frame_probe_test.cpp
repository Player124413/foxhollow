#include "../extern/aurora/lib/gfx/frame_probe.hpp"
#include <cassert>
#include <vector>

int main() {
  using namespace aurora::gfx;
  for (uint32_t width : {1u, 63u, 64u, 65u, 2652u}) {
    const auto pitch = probe_row_pitch(width);
    assert(pitch % 256 == 0 && pitch >= width * 4);
    std::vector<uint8_t> data(pitch * 3u, 255);
    for (uint32_t row = 0; row < 3; ++row) {
      for (uint32_t x = 0; x < width; ++x) {
        auto* pixel = data.data() + row * pitch + x * 4u;
        pixel[0] = pixel[1] = pixel[2] = 0;
      }
    }
    auto stats = probe_rows(data.data(), width, pitch);
    assert(stats.nonblack == 0 && stats.maxRgb == 0 && stats.nonopaque == 0);
    data[2] = 42;
    data[3] = 0;
    stats = probe_rows(data.data(), width, pitch);
    assert(stats.nonblack == 1 && stats.maxRgb == 42 && stats.nonopaque == 1);
  }
  const uint32_t width = 65, height = 17, pitch = probe_row_pitch(width);
  std::vector<uint8_t> image(pitch * height, 255);
  for (uint32_t y = 0; y < height; ++y) for (uint32_t x = 0; x < width; ++x) {
    auto* p = image.data() + y * pitch + x * 4;
    p[0] = p[1] = p[2] = 0;
  }
  image[15 * pitch + 64 * 4 + 1] = 203;
  auto stats = probe_rows(image.data(), width, pitch, height);
  assert(stats.nonblack == 1 && stats.maxRgb == 203 && stats.nonopaque == 0);
  assert(probe_rows(image.data(),width,pitch).nonblack == 0);

}
