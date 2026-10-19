#include "../extern/aurora/lib/gfx/vertex_data_layout.hpp"

#include <cassert>

int main() {
  wgpu::BindGroupLayoutEntry oldEntry{};
  assert(oldEntry.buffer.type == wgpu::BufferBindingType::BindingNotUsed);
  oldEntry.buffer = {};
  assert(oldEntry.buffer.type == wgpu::BufferBindingType::Undefined);
  assert(oldEntry.buffer.type != wgpu::BufferBindingType::BindingNotUsed);

  for (bool texture : {false, true}) {
    const auto entries = aurora::gfx::vertex_data_layout_entries(texture);
    for (uint32_t i = 0; i < entries.size(); ++i) {
      const auto& entry = entries[i];
      assert(entry.binding == i);
      assert(entry.visibility == (i == 0 ? wgpu::ShaderStage::Vertex
                                        : wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment));
      assert(entry.nextInChain == nullptr);
      assert(entry.sampler.type == wgpu::SamplerBindingType::BindingNotUsed);
      assert(entry.storageTexture.access == wgpu::StorageTextureAccess::BindingNotUsed);
      if (texture) {
        assert(entry.buffer.type == wgpu::BufferBindingType::BindingNotUsed);
        assert(entry.texture.sampleType == wgpu::TextureSampleType::Uint);
        assert(entry.texture.viewDimension == wgpu::TextureViewDimension::e2D);
        assert(!entry.texture.multisampled);
      } else {
        assert(entry.buffer.type == wgpu::BufferBindingType::ReadOnlyStorage);
        assert(entry.texture.sampleType == wgpu::TextureSampleType::BindingNotUsed);
        assert(!entry.buffer.hasDynamicOffset);
        assert(entry.buffer.minBindingSize == 0);
      }
      const unsigned bindingTypes =
          (entry.buffer.type != wgpu::BufferBindingType::BindingNotUsed) +
          (entry.sampler.type != wgpu::SamplerBindingType::BindingNotUsed) +
          (entry.texture.sampleType != wgpu::TextureSampleType::BindingNotUsed) +
          (entry.storageTexture.access != wgpu::StorageTextureAccess::BindingNotUsed);
      assert(bindingTypes == 1);
    }
  }
}
