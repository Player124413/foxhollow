#pragma once

#include "frame_packet.hpp"

namespace aurora::gfx {

bool bind_pipeline(PipelineRef ref, const wgpu::RenderPassEncoder& pass);
void bind_pipeline(const wgpu::RenderPipeline& pipeline, const wgpu::RenderPassEncoder& pass);
void bind_texture_group(BindGroupRef ref, const wgpu::RenderPassEncoder& pass);

namespace detail {
bool encode_deferred_frame(wgpu::CommandEncoder& encoder, FramePacket& frame);
void encode_op(wgpu::CommandEncoder& encoder, FramePacket& frame, const FrameOp& op);
}

} // namespace aurora::gfx
