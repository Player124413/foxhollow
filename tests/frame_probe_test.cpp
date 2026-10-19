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
}
