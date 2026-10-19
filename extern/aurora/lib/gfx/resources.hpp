#pragma once

#include "types.hpp"
#include "staging_layout.hpp"

namespace aurora::gfx {

namespace detail {
struct FrameTimings {
  double acquireMs = 0;
  double finishMs = 0;
  double submitMs = 0;
  double presentMs = 0;
};

struct Resources {
  FrameTimings timings;
  wgpu::Buffer vertexBuffer;
  wgpu::Buffer uniformBuffer;
  wgpu::Buffer indexBuffer;
  wgpu::Buffer storageBuffer;
  wgpu::Texture vertexDataTexture;
  wgpu::Texture storageDataTexture;
  wgpu::BindGroupLayout staticBindGroupLayout;
  wgpu::BindGroup staticBindGroup;
  wgpu::BindGroupLayout uniformBindGroupLayout;
  wgpu::BindGroup uniformBindGroup;
  wgpu::Limits limits;
  AuroraStats stats{};
};

Resources& resources() noexcept;
} // namespace detail
} // namespace aurora::gfx
