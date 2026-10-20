#include "driver_probe.hpp"
#include "gpu.hpp"
#include <array>
#include <cstdint>
#include <memory>

namespace aurora::webgpu {
DriverProbeResult run_driver_probe() {
  constexpr std::array<uint32_t,16> values{4093,9,32768,0,64,128,256,512,
                                         1024,2048,4096,8192,16384,65536,131072,262144};
  const wgpu::TextureDescriptor colorDesc{
      .usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc,
      .dimension = wgpu::TextureDimension::e2D, .size = {4,4,1}, .format = wgpu::TextureFormat::RGBA8Unorm,
  };
  auto color = g_device.CreateTexture(&colorDesc);
  auto colorView = color.CreateView();
  const wgpu::BufferDescriptor readDesc{
      .usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead, .size = 6144,
  };
  auto read = g_device.CreateBuffer(&readDesc);
  wgpu::Limits limits{};
  g_device.GetLimits(&limits);
  const uint32_t alignment = limits.minUniformBufferOffsetAlignment;
  const wgpu::BufferDescriptor uniformDesc{
      .usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst,
      .size = static_cast<uint64_t>(alignment) + 3840,
  };
  auto uniform = g_device.CreateBuffer(&uniformDesc);
  g_queue.WriteBuffer(uniform, alignment, values.data(), sizeof(values));
  constexpr std::array<uint32_t,4> control{42,43,44,45};
  g_queue.WriteBuffer(uniform, 0, control.data(), sizeof(control));
  const wgpu::BufferDescriptor indexDesc{
      .usage = wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst, .size = 8,
  };
  auto index = g_device.CreateBuffer(&indexDesc);
  constexpr std::array<uint16_t,4> indices{0,1,2,0};
  g_queue.WriteBuffer(index, 0, indices.data(), sizeof(indices));
  for (uint32_t test = 0; test < 6; ++test) {
    auto encoder = g_device.CreateCommandEncoder();
    const uint32_t mode = test == 1 || test == 2 ? test : 0;
    const auto source = driver_probe_source(mode);
    wgpu::ShaderSourceWGSL wgsl{};
    wgsl.code = source.c_str();
    const wgpu::ShaderModuleDescriptor shaderDesc{.nextInChain = &wgsl};
    auto shader = g_device.CreateShaderModule(&shaderDesc);
    const wgpu::BindGroupLayoutEntry entry{
        .binding = 0, .visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment,
        .buffer = {.type = wgpu::BufferBindingType::Uniform, .hasDynamicOffset = true, .minBindingSize = 64},
    };
    auto parameterEntry = entry;
    parameterEntry.binding = 1;
    parameterEntry.buffer.minBindingSize = 64;
    auto controlEntry = entry;
    controlEntry.buffer.minBindingSize = 16;
    const std::array layoutEntries{controlEntry,parameterEntry};
    const wgpu::BindGroupLayoutDescriptor bindLayoutDesc{.entryCount = mode == 2 ? 2u : 1u, .entries = layoutEntries.data()};
    auto bindLayout = g_device.CreateBindGroupLayout(&bindLayoutDesc);
    const wgpu::PipelineLayoutDescriptor layoutDesc{
        .bindGroupLayoutCount = mode != 0 ? 1u : 0u,
        .bindGroupLayouts = mode != 0 ? &bindLayout : nullptr,
        .immediateSize = mode == 1 ? 64u : 0u,
    };
    auto layout = g_device.CreatePipelineLayout(&layoutDesc);
    const wgpu::ColorTargetState target{.format = wgpu::TextureFormat::RGBA8Unorm};
    const wgpu::FragmentState fragment{
        .module = shader, .entryPoint = "fs_main", .targetCount = 1, .targets = &target,
    };
    const bool hasDepth = test == 3 || test == 4;
    const auto format = test == 4 ? wgpu::TextureFormat::Depth24Plus : wgpu::TextureFormat::Depth32Float;
    const wgpu::DepthStencilState depthState{
        .format = format, .depthWriteEnabled = true, .depthCompare = wgpu::CompareFunction::Greater,
    };
    const wgpu::RenderPipelineDescriptor pipelineDesc{
        .layout = layout,
        .vertex = {.module = shader, .entryPoint = "vs_main"},
        .primitive = {.topology = wgpu::PrimitiveTopology::TriangleList},
        .depthStencil = hasDepth ? &depthState : nullptr,
        .fragment = &fragment,
    };
    auto pipeline = g_device.CreateRenderPipeline(&pipelineDesc);
    const wgpu::RenderPassColorAttachment attachment{
        .view = colorView, .loadOp = wgpu::LoadOp::Clear, .storeOp = wgpu::StoreOp::Store,
        .clearValue = {0,0,0,1},
    };
    wgpu::Texture depth;
    wgpu::TextureView depthView;
    wgpu::RenderPassDepthStencilAttachment depthAttachment{};
    if (hasDepth) {
      const wgpu::TextureDescriptor depthDesc{
          .usage = wgpu::TextureUsage::RenderAttachment, .dimension = wgpu::TextureDimension::e2D, .size = {4,4,1}, .format = format,
      };
      depth = g_device.CreateTexture(&depthDesc);
      depthView = depth.CreateView();
      depthAttachment = {
          .view = depthView, .depthLoadOp = wgpu::LoadOp::Clear,
          .depthStoreOp = wgpu::StoreOp::Store, .depthClearValue = 0,
      };
    }
    const wgpu::RenderPassDescriptor passDesc{
        .colorAttachmentCount = 1, .colorAttachments = &attachment,
        .depthStencilAttachment = hasDepth ? &depthAttachment : nullptr,
    };
    auto pass = encoder.BeginRenderPass(&passDesc);
    if (mode == 1) pass.SetImmediates(0, values.data(), sizeof(values));
    if (mode != 0) {
      const std::array entries{
          wgpu::BindGroupEntry{.binding = 0, .buffer = uniform, .size = 3840},
          wgpu::BindGroupEntry{.binding = 1, .buffer = uniform, .size = 64},
      };
      const wgpu::BindGroupDescriptor bindDesc{.layout = bindLayout, .entryCount = mode == 2 ? 2u : 1u, .entries = entries.data()};
      auto bind = g_device.CreateBindGroup(&bindDesc);
      const std::array<uint32_t,2> offsets{0,alignment};
      pass.SetBindGroup(0, bind, mode == 2 ? 2u : 1u, offsets.data());
    }
    pass.SetPipeline(pipeline);
    pass.SetViewport(0,0,4,4,0,1);
    pass.SetScissorRect(0,0,4,4);
    if (test == 5) {
      pass.SetIndexBuffer(index, wgpu::IndexFormat::Uint16, 0, 8);
      pass.DrawIndexed(3);
    } else pass.Draw(3);
    pass.End();
    const wgpu::TexelCopyTextureInfo src{.texture = color};
    const wgpu::TexelCopyBufferInfo dst{
        .layout = {.offset = test * 1024u, .bytesPerRow = 256, .rowsPerImage = 4}, .buffer = read,
    };
    const wgpu::Extent3D extent{4,4,1};
    encoder.CopyTextureToBuffer(&src, &dst, &extent);
    auto command = encoder.Finish();
    g_queue.Submit(1, &command);
  }
  struct State { bool mapped = false; std::array<bool,6> passed{}; std::array<std::array<uint8_t,4>,6> pixels{}; };
  auto state = std::make_shared<State>();
  auto future = read.MapAsync(wgpu::MapMode::Read, 0, 6144, wgpu::CallbackMode::WaitAnyOnly,
      [read, state](wgpu::MapAsyncStatus status, wgpu::StringView) {
        if (status != wgpu::MapAsyncStatus::Success) return;
        const auto* data = static_cast<const uint8_t*>(read.GetConstMappedRange(0,6144));
        for (uint32_t test = 0; test < 6; ++test) {
          for (uint32_t c = 0; c < 4; ++c) state->pixels[test][c] = data[test * 1024 + c];
          bool valid = true;
          for (uint32_t y = 0; y < 4; ++y) for (uint32_t x = 0; x < 4; ++x) {
            const auto* pixel = data + test * 1024 + y * 256 + x * 4;
            valid &= driver_probe_pixel(pixel[0], pixel[1], pixel[2], pixel[3]);
          }
          state->passed[test] = valid;
        }
        state->mapped = true;
        read.Unmap();
      });
  if (g_instance.WaitAny(future, 5000000000) != wgpu::WaitStatus::Success || !state->mapped) return {};
  return {state->passed[0],state->passed[1],state->passed[2],state->passed[3],state->passed[4],state->passed[5],true,state->pixels};
}
}
