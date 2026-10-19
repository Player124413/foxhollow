#include "../extern/aurora/lib/gfx/staging_layout.hpp"
#include "../extern/aurora/lib/gfx/linear_texture.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace aurora::gfx;

int main() {
  assert(can_batch_shared_uploads(true, false, false));
  assert(!can_batch_shared_uploads(false, false, false));
  assert(!can_batch_shared_uploads(true, true, false));
  assert(!can_batch_shared_uploads(true, false, true));
  assert(!can_batch_shared_uploads(true, true, true));
  std::array<uint32_t, 256> immutable{}, incremental{}, batched{};
  for (uint32_t i = 0; i < immutable.size(); ++i) {
    immutable[i] = i * 73u + 1u;
  }
  batched = immutable;
  uint32_t copied = 0;
  unsigned incrementalCopies = 0;
  for (uint32_t highWater : {3u, 5u, 8u, 15u, 31u, 64u, 128u, 256u}) {
    std::copy(immutable.begin() + copied, immutable.begin() + highWater, incremental.begin() + copied);
    ++incrementalCopies;
    assert(std::equal(incremental.begin(), incremental.begin() + highWater, batched.begin()));
    copied = highWater;
  }
  assert(incrementalCopies == 8);

  const std::array offsets{VertexStagingOffset, UniformStagingOffset, IndexStagingOffset,
                           StorageStagingOffset, TextureUploadStagingOffset};
  const std::array capacities{VertexBufferSize, UniformBufferSize, IndexBufferSize,
                              StorageBufferSize, TextureUploadSize};
  std::vector<uint8_t> staging(TextureUploadStagingOffset + TextureUploadSize);
  for (size_t i = 0; i < offsets.size(); ++i) {
    assert(offsets[i] % 256 == 0);
    assert(staging_upload_size(capacities[i]) == capacities[i]);
    if (i != 0) {
      assert(offsets[i] == offsets[i - 1] + capacities[i - 1]);
    }
    for (uint64_t size : {0ull, 1ull, 3ull, 4ull, 5ull, 4095ull, 4096ull, 4097ull}) {
      const auto padded = staging_upload_size(size);
      assert(padded % 4 == 0);
      assert(padded >= size && padded - size < 4);
      assert(padded <= capacities[i]);
      std::vector<uint8_t> cpu(padded, 0);
      for (uint64_t j = 0; j < size; ++j) {
        cpu[j] = static_cast<uint8_t>(j * 73 + i);
      }
      if (padded != 0) {
        std::memcpy(staging.data() + offsets[i], cpu.data(), padded);
        assert(std::memcmp(staging.data() + offsets[i], cpu.data(), padded) == 0);
      }
      if (i == 0 || i == 3) {
        uint32_t copied = 0;
        std::vector<uint8_t> texture(8192);
        for (uint32_t high : {0u, 1u, 4096u, 4097u}) {
          if (high > size) {
            continue;
          }
          const auto range = linear_texture_copy_range(copied, high);
          if (range.rows != 0) {
            std::memcpy(texture.data() + range.offset,
                        staging.data() + offsets[i] + range.offset,
                        range.rows * LinearTextureBytesPerRow);
          }
          if (high != 0) {
            assert(std::memcmp(texture.data(), cpu.data(), high) == 0);
          }
          copied = high;
        }
      }
    }
  }
}
