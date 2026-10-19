#pragma once

#include "../webgpu/wgpu.hpp"

#include <array>

namespace aurora::gfx {
inline std::array<wgpu::BindGroupLayoutEntry, 2> vertex_data_layout_entries(bool textureFetch) {
  std::array<wgpu::BindGroupLayoutEntry, 2> entries{};
  for (uint32_t i = 0; i < entries.size(); ++i) {
    entries[i].binding = i;
    entries[i].visibility = i == 0 ? wgpu::ShaderStage::Vertex
                                 : wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
    entries[i].buffer.type = textureFetch ? wgpu::BufferBindingType::BindingNotUsed
                                         : wgpu::BufferBindingType::ReadOnlyStorage;
    if (textureFetch) {
      entries[i].texture = {
          .sampleType = wgpu::TextureSampleType::Uint,
          .viewDimension = wgpu::TextureViewDimension::e2D,
      };
    }
  }
  return entries;
}
}
