#include "encoding.hpp"

#include "frame.hpp"
#include "linear_texture.hpp"
#include "state_cache.hpp"

#include "clear.hpp"
#include "depth_peek.hpp"
#include "pipeline_cache.hpp"
#include "resource_cache.hpp"
#include "tex_copy_conv.hpp"
#include "tex_palette_conv.hpp"
#include "../gx/gx.hpp"
#include "../gx/pipeline.hpp"
#include "../gx/viewport_depth.hpp"
#ifdef AURORA_ENABLE_RMLUI
#include "../rmlui/pipeline.hpp"
#endif
#include "../webgpu/gpu.hpp"
#include "../webgpu/gpu_prof.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <tracy/Tracy.hpp>

namespace aurora::gfx {
using namespace detail;
using webgpu::g_device;
using webgpu::g_queue;

namespace {
constexpr Module Log{"aurora::gfx"};
WGPURenderPipeline g_currentPipeline = nullptr;
StateCache<BindGroupRef> g_textureGroupCache;

struct RasterProbe {
  uint32_t draw;
  gx::DrawData data;
  Viewport viewport;
  ClipRect scissor;
};

void resolve_raster_probes(wgpu::CommandEncoder& cmd, FramePacket& frame, const wgpu::QuerySet& queries,
                           const std::vector<RasterProbe>& probes, uint32_t passIndex) {
  if (probes.empty()) return;
  const uint64_t bytes = probes.size() * sizeof(uint64_t);
  const wgpu::BufferDescriptor resolveDesc{
      .usage = wgpu::BufferUsage::QueryResolve | wgpu::BufferUsage::CopySrc, .size = 256,
  };
  const wgpu::BufferDescriptor readDesc{
      .usage = wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst, .size = bytes,
  };
  auto resolved = g_device.CreateBuffer(&resolveDesc);
  auto read = g_device.CreateBuffer(&readDesc);
  cmd.ResolveQuerySet(queries, 0, static_cast<uint32_t>(probes.size()), resolved, 0);
  cmd.CopyBufferToBuffer(resolved, 0, read, 0, bytes);
  const auto frameId = frame.frameIndex + 1;
  frame.afterSubmitCallbacks.emplace_back([read, probes, bytes, frameId, passIndex] {
    read.MapAsync(wgpu::MapMode::Read, 0, bytes, wgpu::CallbackMode::AllowSpontaneous,
        [read, probes, bytes, frameId, passIndex](wgpu::MapAsyncStatus status, wgpu::StringView) {
          if (status != wgpu::MapAsyncStatus::Success) {
            Log.warn("GX raster: frame={} pass={} map_status={}", frameId, passIndex, underlying(status));
            return;
          }
          const auto* samples = static_cast<const uint64_t*>(read.GetConstMappedRange(0, bytes));
          for (size_t i = 0; i < probes.size(); ++i) {
            const auto& p = probes[i];
            const auto& v = p.viewport;
            const auto& s = p.scissor;
            Log.info("GX raster: frame={} pass={} draw={} pipeline={:x} vertices={} indices={} viewport={},{},{},{},{},{} scissor={},{},{},{} samples={}",
                     frameId,passIndex,p.draw,p.data.pipeline,p.data.vtxCount,p.data.indexCount,
                     v.left,v.top,v.width,v.height,v.znear,v.zfar,s.x,s.y,s.width,s.height,samples[i]);
          }
          read.Unmap();
        });
  });
}

void apply_viewport(const wgpu::RenderPassEncoder& pass, const Viewport& vp) {
  const auto depth = gx::viewport_depth(vp.znear, vp.zfar, gx::UseReversedZ);
  pass.SetViewport(vp.left, vp.top, vp.width, vp.height, depth.min, depth.max);
}

void apply_scissor(const wgpu::RenderPassEncoder& pass, const ClipRect& sc, const wgpu::Extent3D& size) {
  const auto x = std::clamp(static_cast<uint32_t>(sc.x), 0u, size.width);
  const auto y = std::clamp(static_cast<uint32_t>(sc.y), 0u, size.height);
  const auto w = std::clamp(static_cast<uint32_t>(sc.width), 0u, size.width - x);
  const auto h = std::clamp(static_cast<uint32_t>(sc.height), 0u, size.height - y);
  pass.SetScissorRect(x, y, w, h);
}

DrawContext make_draw_context(const RenderPass& passInfo) {
  auto& res = resources();
  return {
      .device = g_device,
      .queue = g_queue,
      .vertexBuffer = res.vertexBuffer,
      .indexBuffer = res.indexBuffer,
      .uniformBuffer = res.uniformBuffer,
      .storageBuffer = res.storageBuffer,
      .layout = passInfo.target_layout(),
  };
}

void render_custom_draw(const CustomDrawCommand& draw, const wgpu::RenderPassEncoder& pass,
                        const RenderPass& passInfo) {
  const auto drawType = find_runtime_draw_type(draw.type);
  if (!drawType) {
    // Unregistered between record and replay; the command is a no-op.
    return;
  }

  const auto context = make_draw_context(passInfo);
  drawType->draw(context, pass, draw.payload.data(), draw.payloadSize, drawType->userdata);
}

void execute_encoder_task(wgpu::CommandEncoder& cmd, FramePacket& frame, const EncoderTask& task) {
  const auto taskType = find_runtime_encoder_task_type(task.type);
  if (!taskType) {
    // Unregistered between record and encode; the task is a no-op.
    return;
  }

  auto& res = resources();
  const EncoderTaskContext context{
      .device = g_device,
      .queue = g_queue,
      .vertexBuffer = res.vertexBuffer,
      .indexBuffer = res.indexBuffer,
      .uniformBuffer = res.uniformBuffer,
      .storageBuffer = res.storageBuffer,
  };
  taskType->callback(context, cmd, task.payload.data(), task.payloadSize, taskType->userdata);
  if (taskType->afterSubmit != nullptr) {
    const auto payload = task.payload;
    const auto payloadSize = task.payloadSize;
    const auto callback = taskType->afterSubmit;
    const auto userdata = taskType->userdata;
    frame.afterSubmitCallbacks.emplace_back([payload, payloadSize, callback, userdata] {
      const EncoderTaskCompletionContext completionContext{
          .device = g_device,
          .queue = g_queue,
      };
      callback(completionContext, payload.data(), payloadSize, userdata);
    });
  }
}

void render_pass(const wgpu::RenderPassEncoder& pass, FramePacket& frame, RenderPass& passInfo,
                 [[maybe_unused]] uint32_t passIndex, std::vector<RasterProbe>* probes = nullptr) {
  ZoneScoped;
  g_currentPipeline = nullptr;
  g_textureGroupCache.invalidate();
#ifdef AURORA_GFX_DEBUG_GROUPS
  std::vector<std::string> lastDebugGroupStack;
#endif
  Viewport currentViewport{};
  ClipRect currentScissor{};
  bool hasViewport = false;
  bool hasScissor = false;
  uint32_t gxDraw = 0;
  [[maybe_unused]] uint32_t inspectedDraws = 0;

  // Bind bind group for the whole pass
  pass.SetBindGroup(0, resources().staticBindGroup);
  pass.SetBindGroup(2, gx::g_emptyTextureBindGroup);

  for (auto& cmd : passInfo.commands) {
#ifdef AURORA_GFX_DEBUG_GROUPS
    {
      size_t firstDiff = lastDebugGroupStack.size();
      for (size_t i = 0; i < lastDebugGroupStack.size(); ++i) {
        if (i >= cmd.debugGroupStack.size() || cmd.debugGroupStack[i] != lastDebugGroupStack[i]) {
          firstDiff = i;
          break;
        }
      }
      for (size_t i = firstDiff; i < lastDebugGroupStack.size(); ++i) {
        pass.PopDebugGroup();
      }
      for (size_t i = firstDiff; i < cmd.debugGroupStack.size(); ++i) {
        pass.PushDebugGroup(cmd.debugGroupStack[i].c_str());
      }
      lastDebugGroupStack = cmd.debugGroupStack;
    }
#endif
    switch (cmd.type) {
    case CommandType::SetViewport: {
      const auto& vp = cmd.data.setViewport;
      apply_viewport(pass, vp);
      currentViewport = vp;
      hasViewport = true;
    } break;
    case CommandType::SetScissor: {
      const auto& sc = cmd.data.setScissor;
      apply_scissor(pass, sc, passInfo.colorAttachments[SceneColorAttachmentIndex].size);
      currentScissor = sc;
      hasScissor = true;
    } break;
    case CommandType::Draw: {
      auto& draw = cmd.data.draw;
#ifdef __ANDROID__
      if (draw.gx && (frame.frameIndex == 599 || frame.frameIndex == 899) && passIndex < 16 &&
          inspectedDraws < 16 && (gxDraw < 4 || gxDraw % 32 == 0)) {
        ++inspectedDraws;
        const auto& v = currentViewport;
        const auto& s = currentScissor;
        Log.info("GX raster state: frame={} pass={} draw={} viewport={},{},{},{},{},{} scissor={},{},{},{} occlusion={}",
                 frame.frameIndex+1,passIndex,gxDraw,v.left,v.top,v.width,v.height,v.znear,v.zfar,
                 s.x,s.y,s.width,s.height,webgpu::g_rasterProbeSupported);
      }
#endif
      const bool query = draw.gx && probes != nullptr && probes->size() < 16 &&
                         (gxDraw < 4 || gxDraw % 32 == 0);
      if (query) {
        gx::DrawData data{};
        std::memcpy(&data, draw.payload.data(), sizeof(data));
        pass.BeginOcclusionQuery(static_cast<uint32_t>(probes->size()));
        probes->push_back({gxDraw, data, currentViewport, currentScissor});
      }
      if (draw.encoder != nullptr) {
        draw.encoder(draw.payload.data(), pass, passInfo);
      }
      if (query) pass.EndOcclusionQuery();
      if (draw.gx) ++gxDraw;
    } break;
    case CommandType::CustomDraw: {
      render_custom_draw(cmd.data.customDraw, pass, passInfo);
      g_textureGroupCache.invalidate();
      g_currentPipeline = nullptr;
      pass.SetBindGroup(0, resources().staticBindGroup);
      pass.SetBindGroup(2, gx::g_emptyTextureBindGroup);
      if (hasViewport) {
        apply_viewport(pass, currentViewport);
      }
      if (hasScissor) {
        apply_scissor(pass, currentScissor, passInfo.colorAttachments[SceneColorAttachmentIndex].size);
      }
    } break;
    case CommandType::DebugMarker: {
#if defined(AURORA_GFX_DEBUG_GROUPS)
      pass.InsertDebugMarker(wgpu::StringView(frame.debugMarkers[cmd.data.debugMarkerIndex]));
#endif
    } break;
    }
  }

#ifdef AURORA_GFX_DEBUG_GROUPS
  for (size_t i = 0; i < lastDebugGroupStack.size(); ++i) {
    pass.PopDebugGroup();
  }
#endif
}

void render(wgpu::CommandEncoder& cmd, FramePacket& frame, RenderPass& passInfo, uint32_t passIndex) {
  ZoneScoped;
  if (!passInfo.sealed) {
    return;
  }

  for (const auto& conv : passInfo.paletteConvs) {
    tex_palette_conv::run(cmd, conv);
  }
  if (passInfo.discardable) {
    // This pass has no effect and can be safely discarded (e.g. an empty EFB segment between two back-to-back pass
    // breaks, or an unresolved offscreen pass).
    return;
  }

  std::array<wgpu::RenderPassColorAttachment, MaxColorAttachments> attachments{};
  for (uint32_t i = 0; i < passInfo.colorAttachmentCount; ++i) {
    const auto& source = passInfo.colorAttachments[i];
    attachments[i] = {
        .view = source.view,
        .resolveTarget = source.resolveView,
        .loadOp = source.loadOp != wgpu::LoadOp::Undefined ? source.loadOp
                                                           : (source.clear ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load),
        .storeOp = source.storeOp,
        .clearValue =
            {
                .r = source.clearValue.x(),
                .g = source.clearValue.y(),
                .b = source.clearValue.z(),
                .a = source.clearValue.w(),
            },
    };
  }
  wgpu::RenderPassDepthStencilAttachment depthStencilAttachment{};
  const wgpu::RenderPassDepthStencilAttachment* depthStencilAttachmentPtr = nullptr;
  if (passInfo.depthStencilView) {
    depthStencilAttachment = {
        .view = passInfo.depthStencilView,
        .depthLoadOp = passInfo.hasDepth ? (passInfo.depthLoadOp != wgpu::LoadOp::Undefined
                                                ? passInfo.depthLoadOp
                                                : (passInfo.clearDepth ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load))
                                         : wgpu::LoadOp::Undefined,
        .depthStoreOp = passInfo.hasDepth ? passInfo.depthStoreOp : wgpu::StoreOp::Undefined,
        .depthClearValue = passInfo.clearDepthValue,
        .stencilLoadOp = passInfo.hasStencil ? passInfo.stencilLoadOp : wgpu::LoadOp::Undefined,
        .stencilStoreOp = passInfo.hasStencil ? passInfo.stencilStoreOp : wgpu::StoreOp::Undefined,
        .stencilClearValue = passInfo.stencilClearValue,
    };
    depthStencilAttachmentPtr = &depthStencilAttachment;
  }
  const auto label = passInfo.label.empty() ? fmt::format("Render pass {}", passIndex)
                                            : fmt::format("{} {}", passInfo.label, passIndex);
  wgpu::QuerySet queries;
  std::vector<RasterProbe> probes;
#ifdef __ANDROID__
  if ((frame.frameIndex == 599 || frame.frameIndex == 899) && passIndex < 16) {
    const wgpu::QuerySetDescriptor queryDesc{.type = wgpu::QueryType::Occlusion, .count = 16};
    if (webgpu::g_rasterProbeSupported) queries = g_device.CreateQuerySet(&queryDesc);
    const auto& c = passInfo.colorAttachments[SceneColorAttachmentIndex];
    Log.info("GX pass: frame={} pass={} label={} size={}x{} color_load={} color_store={} clear={},{},{},{} depth_clear={} depth_load={}",
             frame.frameIndex+1,passIndex,label,c.size.width,c.size.height,underlying(attachments[0].loadOp),
             underlying(c.storeOp),c.clearValue.x(),c.clearValue.y(),c.clearValue.z(),c.clearValue.w(),
             passInfo.clearDepthValue,underlying(depthStencilAttachment.depthLoadOp));
  }
#endif
  const wgpu::RenderPassDescriptor renderPassDescriptor{
      .label = label.c_str(),
      .colorAttachmentCount = passInfo.colorAttachmentCount,
      .colorAttachments = attachments.data(),
      .depthStencilAttachment = depthStencilAttachmentPtr,
      .occlusionQuerySet = queries,
      .timestampWrites = webgpu::gpu_prof::pass_writes(label),
  };

  auto pass = cmd.BeginRenderPass(&renderPassDescriptor);
  render_pass(pass, frame, passInfo, passIndex, queries ? &probes : nullptr);
  pass.End();
  if (queries) resolve_raster_probes(cmd, frame, queries, probes, passIndex);

  if (passInfo.captureDepthSnapshot) {
    depth_peek::encode_frame_snapshot(cmd, passInfo.copySourceDepthView,
                                      passInfo.colorAttachments[SceneColorAttachmentIndex].size, passInfo.msaaSamples);
  }

  if (passInfo.resolveTarget) {
    const auto& dstSize = passInfo.resolveTarget->size;
    const bool needsConversion = tex_copy_conv::needs_conversion(passInfo.resolveFormat);
    const bool needsScaling = passInfo.resolvePartial ||
                              dstSize.width != static_cast<uint32_t>(passInfo.resolveRect.width) ||
                              dstSize.height != static_cast<uint32_t>(passInfo.resolveRect.height);
    const bool isDepth = gx::is_depth_format(passInfo.resolveFormat);
    if (isDepth && passInfo.msaaSamples > 1) {
      Log.fatal("Depth tex copies from multisampled EFB targets are not supported");
    }
    const tex_copy_conv::ConvRequest convReq{
        .fmt = passInfo.resolveFormat,
        .srcFmt = passInfo.resolveSourceFormat,
        .srcView = isDepth ? passInfo.copySourceDepthView : passInfo.copySourceView,
        .uniformRange = passInfo.resolveUniformRange,
        .dst = passInfo.resolveTarget,
        .sampleFilter = needsScaling ? tex_copy_conv::SampleFilter::Linear : tex_copy_conv::SampleFilter::Nearest,
    };
    const bool needsOpaqueAlpha = !isDepth && !gx::efb_has_alpha(convReq.srcFmt);
    const bool sameFormat =
        passInfo.resolveTarget->format == passInfo.colorAttachments[SceneColorAttachmentIndex].format;
    if (needsConversion) {
      tex_copy_conv::run(cmd, convReq);
    } else if (needsScaling || needsOpaqueAlpha || !sameFormat) {
      tex_copy_conv::blit(cmd, convReq);
    } else {
      const webgpu::gpu_prof::Zone zone{cmd, "EFB copy"};
      const wgpu::TexelCopyTextureInfo src{
          .texture = passInfo.copySourceTexture,
          .origin =
              wgpu::Origin3D{
                  .x = static_cast<uint32_t>(passInfo.resolveRect.x),
                  .y = static_cast<uint32_t>(passInfo.resolveRect.y),
              },
      };
      const wgpu::TexelCopyTextureInfo dst{
          .texture = passInfo.resolveTarget->texture,
      };
      const wgpu::Extent3D size{
          .width = static_cast<uint32_t>(passInfo.resolveRect.width),
          .height = static_cast<uint32_t>(passInfo.resolveRect.height),
          .depthOrArrayLayers = 1,
      };
      cmd.CopyTextureToTexture(&src, &dst, &size);
    }
  }

  if (passInfo.snapshotColorDst) {
    const webgpu::gpu_prof::Zone zone{cmd, "Pass snapshot"};
    const wgpu::TexelCopyTextureInfo src{
        .texture = passInfo.copySourceTexture,
    };
    const wgpu::TexelCopyTextureInfo dst{
        .texture = passInfo.snapshotColorDst,
    };
    const wgpu::Extent3D size{
        .width = passInfo.colorAttachments[SceneColorAttachmentIndex].size.width,
        .height = passInfo.colorAttachments[SceneColorAttachmentIndex].size.height,
        .depthOrArrayLayers = 1,
    };
    cmd.CopyTextureToTexture(&src, &dst, &size);
  }
  if (passInfo.snapshotDepthDst) {
    tex_copy_conv::snapshot_depth(cmd, passInfo.copySourceDepthView, passInfo.msaaSamples, passInfo.snapshotDepthDst);
  }
  if (passInfo.snapshotNormalDst) {
    const webgpu::gpu_prof::Zone zone{cmd, "Normal snapshot"};
    const wgpu::TexelCopyTextureInfo src{
        .texture = passInfo.copySourceNormalTexture,
    };
    const wgpu::TexelCopyTextureInfo dst{
        .texture = passInfo.snapshotNormalDst,
    };
    const wgpu::Extent3D size{
        .width = passInfo.colorAttachments[SceneColorAttachmentIndex].size.width,
        .height = passInfo.colorAttachments[SceneColorAttachmentIndex].size.height,
        .depthOrArrayLayers = 1,
    };
    cmd.CopyTextureToTexture(&src, &dst, &size);
  }
}

constexpr uint32_t align_down_copy_offset(uint32_t value) noexcept { return value & ~3u; }

void copy_staging_buffer_range(wgpu::CommandEncoder& cmd, const FramePacket& frame, uint32_t& copied,
                               uint32_t highWater, uint64_t stagingOffset, const wgpu::Buffer& dst) {
  if (highWater <= copied) {
    return;
  }
  const uint32_t copyStart = align_down_copy_offset(copied);
  const uint32_t copyEnd = AURORA_ALIGN(highWater, 4);
  cmd.CopyBufferToBuffer(staging_buffer(frame.stagingBuffer), stagingOffset + copyStart, dst, copyStart,
                         copyEnd - copyStart);
  copied = highWater;
}

void copy_staging_texture_range(wgpu::CommandEncoder& cmd, const FramePacket& frame, uint32_t copied,
                                uint32_t highWater, uint64_t stagingOffset, const wgpu::Texture& dst) {
  const auto range = linear_texture_copy_range(copied, highWater);
  if (range.rows == 0) {
    return;
  }
  const wgpu::TexelCopyBufferInfo src{
      .layout = {
          .offset = stagingOffset + range.offset,
          .bytesPerRow = LinearTextureBytesPerRow,
          .rowsPerImage = range.rows,
      },
      .buffer = staging_buffer(frame.stagingBuffer),
  };
  const wgpu::TexelCopyTextureInfo tex{
      .texture = dst,
      .origin = {0, range.y, 0},
  };
  const wgpu::Extent3D extent{LinearTextureWidth, range.rows, 1};
  cmd.CopyBufferToTexture(&src, &tex, &extent);
}

bool needs_staging_copy(const FramePacket& frame, const FrameOp& op) {
  const auto& highWater = op.highWater;
  if (highWater.verts > frame.copied.verts || highWater.uniforms > frame.copied.uniforms ||
      highWater.indices > frame.copied.indices || highWater.storage > frame.copied.storage) {
    return true;
  }
  if constexpr (UseTextureBuffer) {
    return op.textureUploads.size() > frame.copied.textureUploadCount;
  }
  return false;
}

void copy_shared_staging(wgpu::CommandEncoder& cmd, FramePacket& frame, const StagingHighWater& highWater,
                         bool mirrorVertexBuffers = true) {
  auto& res = resources();
  if (webgpu::g_textureVertexFetch) {
    copy_staging_texture_range(cmd, frame, frame.copied.verts, highWater.verts, VertexStagingOffset,
                               res.vertexDataTexture);
    copy_staging_texture_range(cmd, frame, frame.copied.storage, highWater.storage, StorageStagingOffset,
                               res.storageDataTexture);
  }
  if (mirrorVertexBuffers) {
    copy_staging_buffer_range(cmd, frame, frame.copied.verts, highWater.verts, VertexStagingOffset, res.vertexBuffer);
    copy_staging_buffer_range(cmd, frame, frame.copied.storage, highWater.storage, StorageStagingOffset,
                              res.storageBuffer);
  } else {
    frame.copied.verts = highWater.verts;
    frame.copied.storage = highWater.storage;
  }
  copy_staging_buffer_range(cmd, frame, frame.copied.uniforms, highWater.uniforms, UniformStagingOffset,
                            res.uniformBuffer);
  copy_staging_buffer_range(cmd, frame, frame.copied.indices, highWater.indices, IndexStagingOffset, res.indexBuffer);
}

void copy_staging_to_high_water(wgpu::CommandEncoder& cmd, FramePacket& frame, const FrameOp& op) {
  if (!needs_staging_copy(frame, op)) {
    return;
  }
  const webgpu::gpu_prof::Zone zone{cmd, "Staging copies"};
  const auto& highWater = op.highWater;
  copy_shared_staging(cmd, frame, highWater);

  if constexpr (UseTextureBuffer) {
    for (size_t i = frame.copied.textureUploadCount; i < op.textureUploads.size(); ++i) {
      const auto& item = *op.textureUploads[i];
      const wgpu::TexelCopyBufferInfo buf{
          .layout =
              wgpu::TexelCopyBufferLayout{
                  .offset = item.buffer ? item.layout.offset : item.layout.offset + TextureUploadStagingOffset,
                  .bytesPerRow = AURORA_ALIGN(item.layout.bytesPerRow, 256),
                  .rowsPerImage = item.layout.rowsPerImage,
              },
          .buffer = item.buffer ? item.buffer : staging_buffer(frame.stagingBuffer),
      };
      cmd.CopyBufferToTexture(&buf, &item.tex, &item.size);
    }
    frame.copied.textureUpload = highWater.textureUpload;
    frame.copied.textureUploadCount = op.textureUploads.size();
  }
}
} // namespace

namespace detail {
bool encode_deferred_frame(wgpu::CommandEncoder& cmd, FramePacket& frame) {
  frame.textureOnlyUploads = false;
  if (!frame.deferEncoding) {
    return false;
  }
  bool hasEncoderTask = false;
  bool hasCustomDraw = false;
  for (const auto& op : frame.ops) {
    hasEncoderTask |= op.type == FrameOpType::EncoderTask;
    if (op.renderPass != nullptr) {
      for (const auto& command : op.renderPass->commands) {
        hasCustomDraw |= command.type == CommandType::CustomDraw;
      }
    }
  }
  const bool batched = can_batch_shared_uploads(frame.deferEncoding, hasEncoderTask, hasCustomDraw);
  if (batched) {
    const StagingHighWater highWater{
        .verts = static_cast<uint32_t>(frame.verts.size()),
        .uniforms = static_cast<uint32_t>(frame.uniforms.size()),
        .indices = static_cast<uint32_t>(frame.indices.size()),
        .storage = static_cast<uint32_t>(frame.storage.size()),
    };
#ifdef AURORA_ENABLE_RMLUI
    constexpr bool rmlui = true;
#else
    constexpr bool rmlui = false;
#endif
    const bool textureOnly = can_use_texture_only_uploads(webgpu::g_textureVertexFetch, batched, rmlui);
    copy_shared_staging(cmd, frame, highWater, !textureOnly);
    frame.textureOnlyUploads = textureOnly;
  }
  for (const auto& op : frame.ops) {
    encode_op(cmd, frame, op);
  }
  return batched;
}

void encode_op(wgpu::CommandEncoder& cmd, FramePacket& frame, const FrameOp& op) {
  copy_staging_to_high_water(cmd, frame, op);
  switch (op.type) {
  case FrameOpType::RenderPass:
    if (op.renderPass != nullptr) {
      render(cmd, frame, *op.renderPass, op.index);
    }
    break;
  case FrameOpType::TextureCopy:
    if (op.textureCopy != nullptr) {
      const webgpu::gpu_prof::Zone zone{cmd, "Texture copy"};
      cmd.CopyTextureToTexture(&op.textureCopy->src, &op.textureCopy->dst, &op.textureCopy->size);
    }
    break;
  case FrameOpType::EncoderTask:
    if (op.encoderTask != nullptr) {
      execute_encoder_task(cmd, frame, *op.encoderTask);
    }
    break;
  }
}
} // namespace detail

void bind_texture_group(BindGroupRef ref, const wgpu::RenderPassEncoder& pass) {
#ifndef AURORA_ENABLE_RMLUI
  if (!g_textureGroupCache.update(ref)) {
    return;
  }
#endif
  pass.SetBindGroup(2, find_bind_group(ref));
}

void bind_pipeline(const wgpu::RenderPipeline& pipeline, const wgpu::RenderPassEncoder& pass) {
  if (pipeline.Get() == g_currentPipeline) {
    return;
  }
  pass.SetPipeline(pipeline);
  g_currentPipeline = pipeline.Get();
}

bool bind_pipeline(PipelineRef ref, const wgpu::RenderPassEncoder& pass) {
  CompiledPipeline pipeline;
  if (!get_pipeline(ref, pipeline)) {
    return false;
  }
  AURORA_ASSERT(!pipeline.prepass, "Prepass pipelines require individual binding");
  bind_pipeline(pipeline.main, pass);
  return true;
}
} // namespace aurora::gfx
