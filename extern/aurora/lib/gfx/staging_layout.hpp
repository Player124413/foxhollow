#pragma once

#include <cstdint>

namespace aurora::gfx {
inline constexpr bool UseTextureBuffer = true;
inline constexpr uint64_t UniformBufferSize = 25165824; // 24 MiB
inline constexpr uint64_t VertexBufferSize = 5242880;   // 5 MiB
inline constexpr uint64_t IndexBufferSize = 8388608;    // 8 MiB
inline constexpr uint64_t StorageBufferSize = 8388608;  // 8 MiB
inline constexpr uint64_t TextureUploadSize = 25165824; // 24 MiB

inline constexpr uint64_t VertexStagingOffset = 0;
inline constexpr uint64_t UniformStagingOffset = VertexStagingOffset + VertexBufferSize;
inline constexpr uint64_t IndexStagingOffset = UniformStagingOffset + UniformBufferSize;
inline constexpr uint64_t StorageStagingOffset = IndexStagingOffset + IndexBufferSize;
inline constexpr uint64_t TextureUploadStagingOffset = StorageStagingOffset + StorageBufferSize;

constexpr bool use_queue_staging(bool openGles, bool android, bool textureFetch) {
  return openGles && (android || textureFetch);
}

constexpr bool can_batch_shared_uploads(bool deferred, bool hasEncoderTask, bool hasCustomDraw) {
  return deferred && !hasEncoderTask && !hasCustomDraw;
}

constexpr bool can_use_texture_only_uploads(bool textureFetch, bool batched, bool rmlui) {
  return textureFetch && batched && !rmlui;
}

constexpr uint64_t staging_upload_size(uint64_t size) {
  return (size + 3u) & ~uint64_t{3};
}
}
