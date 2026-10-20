#include "frame.hpp"
#include "encoding.hpp"
#include "linear_texture.hpp"
#include "vertex_data_layout.hpp"

#include "depth_peek.hpp"
#include "pipeline_cache.hpp"
#include "recording.hpp"
#include "render_worker.hpp"
#include "resource_cache.hpp"
#include "tex_copy_conv.hpp"
#include "tex_palette_conv.hpp"
#include "texture_replacement.hpp"
#include "../gx/gx.hpp"
#include "../gx/fifo.hpp"
#ifdef AURORA_ENABLE_RMLUI
#include "../rmlui/pipeline.hpp"
#endif
#include "../webgpu/gpu.hpp"
#include "../webgpu/gpu_prof.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <tracy/Tracy.hpp>
#include <magic_enum.hpp>

namespace aurora::gfx {
using namespace detail;
using webgpu::g_device;
using webgpu::g_instance;
using webgpu::g_queue;

namespace {
constexpr Module Log{"aurora::gfx"};

#ifdef __ANDROID__
void probe_uploaded_buffers(wgpu::CommandEncoder& encoder, FramePacket& frame) {
  if (!frame.deferEncoding || webgpu::g_textureVertexFetch ||
      (frame.frameIndex != 899 && frame.frameIndex != 1199)) return;
  const std::array<const ByteBuffer*, 4> cpu{&frame.verts, &frame.uniforms, &frame.indices, &frame.storage};
  const auto& res = resources();
  const std::array gpu{res.vertexBuffer, res.uniformBuffer, res.indexBuffer, res.storageBuffer};
  std::array<std::array<uint8_t, 64>, 4> expected{};
  std::array<uint32_t, 4> sizes{};
  const wgpu::BufferDescriptor descriptor{
      .label = "GX upload probe",
      .usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead,
      .size = 256,
  };
  auto buffer = g_device.CreateBuffer(&descriptor);
  for (size_t i = 0; i < cpu.size(); ++i) {
    sizes[i] = static_cast<uint32_t>(std::min<size_t>(cpu[i]->size(), 64)) & ~3u;
    if (sizes[i] == 0) continue;
    std::memcpy(expected[i].data(), cpu[i]->data(), sizes[i]);
    encoder.CopyBufferToBuffer(gpu[i], 0, buffer, i * 64, sizes[i]);
  }
  const uint32_t frameId = frame.frameIndex + 1;
  frame.afterSubmitCallbacks.emplace_back([buffer, expected, sizes, frameId] {
    buffer.MapAsync(wgpu::MapMode::Read, 0, 256, wgpu::CallbackMode::AllowSpontaneous,
        [buffer, expected, sizes, frameId](wgpu::MapAsyncStatus status, wgpu::StringView) {
          if (status != wgpu::MapAsyncStatus::Success) {
            Log.warn("Upload probe: frame={} map_status={}", frameId, magic_enum::enum_name(status));
            return;
          }
          const auto* data = static_cast<const uint8_t*>(buffer.GetConstMappedRange(0, 256));
          constexpr std::array names{"vertex", "uniform", "index", "storage"};
          for (size_t i = 0; i < sizes.size(); ++i) {
            if (sizes[i] == 0) continue;
            uint32_t firstBad = 0;
            while (firstBad < sizes[i] && expected[i][firstBad] == data[i * 64 + firstBad]) ++firstBad;
            const bool match = firstBad == sizes[i];
            Log.info("Upload probe: frame={} buffer={} bytes={} match={} first_bad={} cpu={} gpu={}",
                     frameId, names[i], sizes[i], match, firstBad,
                     match ? 0u : expected[i][firstBad], match ? 0u : data[i * 64 + firstBad]);
          }
          buffer.Unmap();
        });
  });
}
#endif

Resources g_resources;
bool g_queueStaging = false;

enum class BufferMapState {
  Unmapped,
  Mapping,
  Mapped,
};

std::array<wgpu::Buffer, StagingBufferCount> g_stagingBuffers;
std::array<std::atomic<BufferMapState>, StagingBufferCount> g_mappingStates;
uint32_t g_frameIndex = UINT32_MAX;

std::array<FramePacket, FrameSlotCount> g_framePackets;
uint64_t g_nextFrameId = 1;
render_worker::FrameSlotPool g_frameSlots{FrameSlotCount};
render_worker::FrameSlotPool g_stagingSlots{StagingBufferCount};

struct RuntimeDrawType {
  std::string label;
  DrawCallback draw = nullptr;
  void* userdata = nullptr;
  uint32_t generation = 1;
};

struct RuntimeEncoderTaskType {
  std::string label;
  EncoderTaskCallback callback = nullptr;
  void* userdata = nullptr;
  EncoderTaskCompletionCallback afterSubmit = nullptr;
  uint32_t generation = 1;
};

constexpr uint32_t draw_type_index(DrawTypeId id) { return static_cast<uint32_t>(id & 0xFFFFFFFFu); }
constexpr uint32_t draw_type_generation(DrawTypeId id) { return static_cast<uint32_t>(id >> 32); }
constexpr DrawTypeId make_draw_type_id(uint32_t index, uint32_t generation) {
  return static_cast<DrawTypeId>(generation) << 32 | index;
}

std::vector<RuntimeDrawType> g_runtimeDrawTypes;
std::vector<uint32_t> g_freeDrawTypeSlots;
std::vector<RuntimeEncoderTaskType> g_runtimeEncoderTaskTypes;
std::vector<uint32_t> g_freeEncoderTaskTypeSlots;
std::mutex g_runtimeTypeMutex;

// Requires g_runtimeTypeMutex held.
const RuntimeDrawType* find_runtime_draw_type_locked(DrawTypeId id) {
  const auto idx = draw_type_index(id);
  if (id == InvalidDrawType || idx >= g_runtimeDrawTypes.size()) {
    return nullptr;
  }
  const auto& slot = g_runtimeDrawTypes[idx];
  if (slot.generation != draw_type_generation(id) || slot.draw == nullptr) {
    return nullptr;
  }
  return &slot;
}

// Requires g_runtimeTypeMutex held.
const RuntimeEncoderTaskType* find_runtime_encoder_task_type_locked(EncoderTaskId id) {
  const auto idx = draw_type_index(id);
  if (id == InvalidEncoderTask || idx >= g_runtimeEncoderTaskTypes.size()) {
    return nullptr;
  }
  const auto& slot = g_runtimeEncoderTaskTypes[idx];
  if (slot.generation != draw_type_generation(id) || slot.callback == nullptr) {
    return nullptr;
  }
  return &slot;
}

using PresentClock = std::chrono::steady_clock;
constexpr auto PresentFpsWindow = std::chrono::seconds{1};
std::mutex g_presentStatsMutex;
std::deque<PresentClock::time_point> g_presentTimes;
std::atomic_bool g_processEventsQueued = false;
std::atomic_int64_t g_lastPresentNs = 0;
std::atomic_int64_t g_presentPeriodNs = 0;
std::atomic_int64_t g_cpuFrameTimeNs = 0;
PresentClock::time_point g_cpuFrameStart;
constexpr auto FrameStartSafetyMargin = std::chrono::milliseconds{2};
constexpr auto MaxPacingSample = std::chrono::milliseconds{250};
constexpr uint32_t PacingEmaWeight = 8;

int64_t timestamp_ns(PresentClock::time_point time) {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(time.time_since_epoch()).count();
}

int64_t duration_ns(PresentClock::duration duration) {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
}

void update_ema(std::atomic_int64_t& value, int64_t sample) {
  if (sample <= 0 || sample > duration_ns(MaxPacingSample)) {
    return;
  }

  int64_t current = value.load(std::memory_order_acquire);
  while (true) {
    const int64_t next = current == 0 ? sample : current + (sample - current) / static_cast<int64_t>(PacingEmaWeight);
    if (value.compare_exchange_weak(current, next, std::memory_order_acq_rel, std::memory_order_acquire)) {
      return;
    }
  }
}

void prune_present_times(PresentClock::time_point now) {
  while (!g_presentTimes.empty() && g_presentTimes.front() + PresentFpsWindow < now) {
    g_presentTimes.pop_front();
  }
}

void process_events() {
  ZoneScopedN("ProcessEvents");
  if (g_instance) {
    g_instance.ProcessEvents();
  }
}

void enqueue_process_events() {
  if (render_worker::is_worker_thread()) {
    process_events();
    return;
  }

  bool expected = false;
  if (!g_processEventsQueued.compare_exchange_strong(expected, true, std::memory_order_acq_rel,
                                                     std::memory_order_acquire)) {
    return;
  }

  render_worker::enqueue_work([] {
    process_events();
    g_processEventsQueued.store(false, std::memory_order_release);
  });
}

void wait_for_gpu_progress(std::chrono::nanoseconds sleepDuration) {
  if (render_worker::is_idle()) {
    enqueue_process_events();
  }
  std::this_thread::sleep_for(sleepDuration);
}

void pace_frame_start() {
  ZoneScopedN("Frame start pacing");
  if (g_frameSlots.free_count() == FrameSlotCount) {
    return;
  }

  const int64_t lastPresentNs = g_lastPresentNs.load(std::memory_order_acquire);
  const int64_t presentPeriodNs = g_presentPeriodNs.load(std::memory_order_acquire);
  const int64_t cpuFrameTimeNs = g_cpuFrameTimeNs.load(std::memory_order_acquire);
  if (lastPresentNs == 0 || presentPeriodNs == 0 || cpuFrameTimeNs == 0) {
    return;
  }

  const int64_t safetyMarginNs = duration_ns(FrameStartSafetyMargin);
  const int64_t targetStartNs = lastPresentNs + presentPeriodNs - cpuFrameTimeNs - safetyMarginNs;
  int64_t nowNs = timestamp_ns(PresentClock::now());
  if (targetStartNs <= nowNs) {
    return;
  }

  const double initialWaitMs = static_cast<double>(targetStartNs - nowNs) / 1'000'000.0;
  TracyPlot("aurora: frameStartPaceWaitMs", initialWaitMs);
  while (nowNs < targetStartNs) {
    const int64_t remainingNs = targetStartNs - nowNs;
    const auto sleepDuration =
        remainingNs > 1'000'000 ? std::chrono::milliseconds{1} : std::chrono::nanoseconds{remainingNs};
    wait_for_gpu_progress(sleepDuration);
    nowNs = timestamp_ns(PresentClock::now());
  }
}

void map_staging_buffer(size_t slot, bool releaseSlotOnCompletion = false) {
  if (g_queueStaging) {
    g_mappingStates[slot].store(BufferMapState::Mapped, std::memory_order_release);
    if (releaseSlotOnCompletion) {
      g_stagingSlots.release(slot);
    }
    return;
  }
  auto expected = BufferMapState::Unmapped;
  if (!g_mappingStates[slot].compare_exchange_strong(expected, BufferMapState::Mapping, std::memory_order_acq_rel,
                                                     std::memory_order_acquire)) {
    return;
  }

  g_stagingBuffers[slot].MapAsync(
      wgpu::MapMode::Write, 0, StagingBufferSize, wgpu::CallbackMode::AllowSpontaneous,
      [slot, releaseSlotOnCompletion](wgpu::MapAsyncStatus status, wgpu::StringView message) {
        if (status == wgpu::MapAsyncStatus::CallbackCancelled || status == wgpu::MapAsyncStatus::Aborted) {
          Log.warn("Buffer mapping {}: {}", magic_enum::enum_name(status), message);
          g_mappingStates[slot].store(BufferMapState::Unmapped, std::memory_order_release);
          if (releaseSlotOnCompletion) {
            g_stagingSlots.release(slot);
          }
          return;
        }
        AURORA_ASSERT(status == wgpu::MapAsyncStatus::Success, "Buffer mapping failed: {} {}",
                      magic_enum::enum_name(status), message);
        g_mappingStates[slot].store(BufferMapState::Mapped, std::memory_order_release);
        if (releaseSlotOnCompletion) {
          g_stagingSlots.release(slot);
        }
      });
}
} // namespace

namespace detail {

Resources& resources() noexcept { return g_resources; }

const wgpu::Buffer& staging_buffer(size_t slot) { return g_stagingBuffers[slot]; }

std::optional<RegisteredDrawType> find_runtime_draw_type(DrawTypeId id) {
  std::lock_guard lock{g_runtimeTypeMutex};
  const auto* slot = find_runtime_draw_type_locked(id);
  if (slot == nullptr) {
    return std::nullopt;
  }
  return RegisteredDrawType{.draw = slot->draw, .userdata = slot->userdata};
}

std::optional<RegisteredEncoderTaskType> find_runtime_encoder_task_type(EncoderTaskId id) {
  std::lock_guard lock{g_runtimeTypeMutex};
  const auto* slot = find_runtime_encoder_task_type_locked(id);
  if (slot == nullptr) {
    return std::nullopt;
  }
  return RegisteredEncoderTaskType{
      .callback = slot->callback,
      .userdata = slot->userdata,
      .afterSubmit = slot->afterSubmit,
  };
}

} // namespace detail

wgpu::Device device() noexcept { return g_device; }

wgpu::Queue queue() noexcept { return g_queue; }

wgpu::TextureFormat color_format() noexcept { return webgpu::g_graphicsConfig.surfaceConfiguration.format; }

wgpu::TextureFormat depth_format() noexcept { return webgpu::g_graphicsConfig.depthFormat; }

uint32_t sample_count() noexcept { return webgpu::g_graphicsConfig.msaaSamples; }

RenderTargetLayout scene_render_target_layout() noexcept {
  RenderTargetLayout layout{
      .colorAttachmentCount = 1,
      .colorAttachments = {{{
          .semantic = ColorAttachmentSemantic::SceneColor,
          .format = webgpu::g_frameBuffer.format,
          .width = webgpu::g_frameBuffer.size.width,
          .height = webgpu::g_frameBuffer.size.height,
      }}},
      .depthStencilFormat = webgpu::g_graphicsConfig.depthFormat,
      .sampleCount = webgpu::g_graphicsConfig.msaaSamples,
  };
  if (webgpu::g_graphicsConfig.normalBuffer) {
    layout.colorAttachments[layout.colorAttachmentCount++] = {
        .semantic = ColorAttachmentSemantic::Normal,
        .format = webgpu::g_normalBuffer.format,
        .width = webgpu::g_normalBuffer.size.width,
        .height = webgpu::g_normalBuffer.size.height,
    };
  }
  finalize_render_target_layout(layout);
  return layout;
}

bool uses_reversed_z() noexcept { return gx::UseReversedZ; }

DrawTypeId register_draw_type(const DrawTypeDescriptor& desc) {
  if (desc.draw == nullptr) {
    Log.warn("register_draw_type: draw callback is null");
    return InvalidDrawType;
  }

  std::lock_guard lock{g_runtimeTypeMutex};
  uint32_t idx;
  if (!g_freeDrawTypeSlots.empty()) {
    idx = g_freeDrawTypeSlots.back();
    g_freeDrawTypeSlots.pop_back();
  } else {
    idx = static_cast<uint32_t>(g_runtimeDrawTypes.size());
    g_runtimeDrawTypes.emplace_back();
  }
  auto& slot = g_runtimeDrawTypes[idx];
  slot.label = desc.label != nullptr ? desc.label : "";
  slot.draw = desc.draw;
  slot.userdata = desc.userdata;
  return make_draw_type_id(idx, slot.generation);
}

void unregister_draw_type(DrawTypeId type) noexcept {
  std::lock_guard lock{g_runtimeTypeMutex};
  if (find_runtime_draw_type_locked(type) == nullptr) {
    return;
  }
  const auto idx = draw_type_index(type);
  auto& slot = g_runtimeDrawTypes[idx];
  slot.label.clear();
  slot.draw = nullptr;
  slot.userdata = nullptr;
  ++slot.generation;
  g_freeDrawTypeSlots.push_back(idx);
}

EncoderTaskId register_encoder_task_type(const EncoderTaskDescriptor& desc) {
  if (desc.callback == nullptr) {
    Log.warn("register_encoder_task_type: callback is null");
    return InvalidEncoderTask;
  }

  std::lock_guard lock{g_runtimeTypeMutex};
  uint32_t idx;
  if (!g_freeEncoderTaskTypeSlots.empty()) {
    idx = g_freeEncoderTaskTypeSlots.back();
    g_freeEncoderTaskTypeSlots.pop_back();
  } else {
    idx = static_cast<uint32_t>(g_runtimeEncoderTaskTypes.size());
    g_runtimeEncoderTaskTypes.emplace_back();
  }
  auto& slot = g_runtimeEncoderTaskTypes[idx];
  slot.label = desc.label != nullptr ? desc.label : "";
  slot.callback = desc.callback;
  slot.userdata = desc.userdata;
  slot.afterSubmit = desc.afterSubmit;
  return make_draw_type_id(idx, slot.generation);
}

void unregister_encoder_task_type(EncoderTaskId type) noexcept {
  std::lock_guard lock{g_runtimeTypeMutex};
  if (find_runtime_encoder_task_type_locked(type) == nullptr) {
    return;
  }
  const auto idx = draw_type_index(type);
  auto& slot = g_runtimeEncoderTaskTypes[idx];
  slot.label.clear();
  slot.callback = nullptr;
  slot.userdata = nullptr;
  slot.afterSubmit = nullptr;
  ++slot.generation;
  g_freeEncoderTaskTypeSlots.push_back(idx);
}

void initialize() {
#ifdef __ANDROID__
  constexpr bool android = true;
#else
  constexpr bool android = false;
#endif
  g_queueStaging = use_queue_staging(webgpu::g_backendType == wgpu::BackendType::OpenGLES, android,
                                     webgpu::g_textureVertexFetch);
  Log.info("Staging upload path: {}", g_queueStaging ? "queue writes" : "mapped buffers");
  g_frameIndex = 0;
  g_processEventsQueued.store(false, std::memory_order_release);
  g_lastPresentNs.store(0, std::memory_order_release);
  g_presentPeriodNs.store(0, std::memory_order_release);
  g_cpuFrameTimeNs.store(0, std::memory_order_release);
  g_cpuFrameStart = {};
  {
    std::lock_guard lock{g_presentStatsMutex};
    g_presentTimes.clear();
  }
  render_worker::initialize();
  // This appears to take a while and blocks the render thread for periods of time
  // render_worker::set_event_pump([] {
  //   if (g_instance) {
  //     g_instance.ProcessEvents();
  //   }
  // });
  depth_peek::initialize();
  tex_copy_conv::initialize();
  tex_palette_conv::initialize();

  // For uniform & storage buffer offset alignments
  g_device.GetLimits(&g_resources.limits);

  const auto createBuffer = [](wgpu::Buffer& out, wgpu::BufferUsage usage, uint64_t size, const char* label) {
    if (size <= 0) {
      return;
    }
#ifdef __ANDROID__
    if (!webgpu::g_textureVertexFetch) usage |= wgpu::BufferUsage::CopySrc;
#endif
    const wgpu::BufferDescriptor descriptor{
        .label = label,
        .usage = usage,
        .size = size,
    };
    out = g_device.CreateBuffer(&descriptor);
  };
  createBuffer(g_resources.uniformBuffer, wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst, UniformBufferSize,
               "Shared Uniform Buffer");
  createBuffer(g_resources.vertexBuffer,
               wgpu::BufferUsage::Storage | wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst, VertexBufferSize,
               "Shared Vertex Buffer");
  createBuffer(g_resources.indexBuffer, wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst, IndexBufferSize,
               "Shared Index Buffer");
  createBuffer(g_resources.storageBuffer, wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst, StorageBufferSize,
               "Shared Storage Buffer");
  for (size_t i = 0; i < g_stagingBuffers.size(); ++i) {
    const auto label = fmt::format("Staging Buffer {}", i);
    const auto usage = wgpu::BufferUsage::CopySrc |
                       (g_queueStaging ? wgpu::BufferUsage::CopyDst : wgpu::BufferUsage::MapWrite);
    createBuffer(g_stagingBuffers[i], usage, StagingBufferSize,
                 label.c_str());
  }
  for (auto& state : g_mappingStates) {
    state.store(BufferMapState::Unmapped, std::memory_order_release);
  }
  for (size_t slot = 0; slot < g_stagingBuffers.size(); ++slot) {
    map_staging_buffer(slot);
  }

  {
    const auto layoutEntries = vertex_data_layout_entries(webgpu::g_textureVertexFetch);
    if (webgpu::g_textureVertexFetch) {
      static_assert(VertexBufferSize % LinearTextureBytesPerRow == 0);
      static_assert(StorageBufferSize % LinearTextureBytesPerRow == 0);
      const auto createDataTexture = [](uint64_t size, const char* label) {
        const wgpu::TextureDescriptor desc{
            .label = label,
            .usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst,
            .dimension = wgpu::TextureDimension::e2D,
            .size = {LinearTextureWidth, static_cast<uint32_t>(size / LinearTextureBytesPerRow), 1},
            .format = wgpu::TextureFormat::RGBA32Uint,
        };
        return g_device.CreateTexture(&desc);
      };
      g_resources.vertexDataTexture = createDataTexture(VertexBufferSize, "Vertex data texture");
      g_resources.storageDataTexture = createDataTexture(StorageBufferSize, "Storage data texture");
    }
    const wgpu::BindGroupLayoutDescriptor layoutDesc{
        .label = "Static bind group layout",
        .entryCount = layoutEntries.size(),
        .entries = layoutEntries.data(),
    };
    g_resources.staticBindGroupLayout = g_device.CreateBindGroupLayout(&layoutDesc);
    std::array entries{
        wgpu::BindGroupEntry{
            .binding = 0,
            .buffer = g_resources.vertexBuffer,
        },
        wgpu::BindGroupEntry{
            .binding = 1,
            .buffer = g_resources.storageBuffer,
        },
    };
    if (webgpu::g_textureVertexFetch) {
      entries[0].buffer = nullptr;
      entries[0].textureView = g_resources.vertexDataTexture.CreateView();
      entries[1].buffer = nullptr;
      entries[1].textureView = g_resources.storageDataTexture.CreateView();
    }
    const wgpu::BindGroupDescriptor bindGroupDescriptor{
        .label = "Static bind group",
        .layout = g_resources.staticBindGroupLayout,
        .entryCount = entries.size(),
        .entries = entries.data(),
    };
    g_resources.staticBindGroup = g_device.CreateBindGroup(&bindGroupDescriptor);
  }

  {
    constexpr std::array layoutEntries{
        // Uniform buffer (dynamic offset)
        wgpu::BindGroupLayoutEntry{
            .binding = 0,
            .visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment,
            .buffer =
                wgpu::BufferBindingLayout{
                    .type = wgpu::BufferBindingType::Uniform,
                    .hasDynamicOffset = true,
                },
        },
    };
    const wgpu::BindGroupLayoutDescriptor layoutDesc{
        .label = "Uniform bind group layout",
        .entryCount = layoutEntries.size(),
        .entries = layoutEntries.data(),
    };
    g_resources.uniformBindGroupLayout = g_device.CreateBindGroupLayout(&layoutDesc);
    const std::array entries{
        wgpu::BindGroupEntry{
            .binding = 0,
            .buffer = g_resources.uniformBuffer,
            .size = gx::MaxUniformSize,
        },
    };
    const wgpu::BindGroupDescriptor bindGroupDescriptor{
        .label = "Uniform bind group",
        .layout = g_resources.uniformBindGroupLayout,
        .entryCount = entries.size(),
        .entries = entries.data(),
    };
    g_resources.uniformBindGroup = g_device.CreateBindGroup(&bindGroupDescriptor);
  }

  gx::initialize();
#ifdef AURORA_ENABLE_RMLUI
  rmlui::initialize_pipeline();
#endif
  initialize_pipeline_cache();
}

void shutdown() {
  render_worker::synchronize();
  render_worker::shutdown();
  g_processEventsQueued.store(false, std::memory_order_release);
  g_lastPresentNs.store(0, std::memory_order_release);
  g_presentPeriodNs.store(0, std::memory_order_release);
  g_cpuFrameTimeNs.store(0, std::memory_order_release);
  g_cpuFrameStart = {};
  {
    std::lock_guard lock{g_presentStatsMutex};
    g_presentTimes.clear();
  }
  shutdown_pipeline_cache();
  depth_peek::shutdown();
  tex_copy_conv::shutdown();
  tex_palette_conv::shutdown();
  texture_replacement::shutdown();
  gx::shutdown();
#ifdef AURORA_ENABLE_RMLUI
  rmlui::shutdown_pipeline();
#endif

  shutdown_resource_cache();
  {
    std::lock_guard lock{g_runtimeTypeMutex};
    g_runtimeDrawTypes.clear();
    g_freeDrawTypeSlots.clear();
    g_runtimeEncoderTaskTypes.clear();
    g_freeEncoderTaskTypeSlots.clear();
  }
  g_resources.vertexBuffer = {};
  g_resources.uniformBuffer = {};
  g_resources.indexBuffer = {};
  g_resources.storageBuffer = {};
  g_resources.vertexDataTexture = {};
  g_resources.storageDataTexture = {};
  g_stagingBuffers.fill({});
  for (auto& packet : g_framePackets) {
    packet = {};
  }
  shutdown_recording();
  g_resources.staticBindGroup = {};
  g_resources.staticBindGroupLayout = {};
  g_resources.uniformBindGroup = {};
  g_resources.uniformBindGroupLayout = {};
  g_frameIndex = UINT32_MAX;
  g_frameSlots.reset();
  g_stagingSlots.reset();
  for (auto& state : g_mappingStates) {
    state.store(BufferMapState::Unmapped, std::memory_order_release);
  }
}

bool wait_for_staging_buffer(size_t slot) {
  ZoneScopedN("Wait for buffer map");
  map_staging_buffer(slot);
  const auto waitStart = PresentClock::now();
  bool reported = false;
  while (true) {
    if (!reported && PresentClock::now() - waitStart >= std::chrono::seconds{2}) {
      Log.warn("Frame stalled: staging map slot={} state={}", slot,
               static_cast<int>(g_mappingStates[slot].load(std::memory_order_acquire)));
      reported = true;
    }
    const auto mappingState = g_mappingStates[slot].load(std::memory_order_acquire);
    if (mappingState == BufferMapState::Mapped) {
      return true;
    }
    if (mappingState == BufferMapState::Unmapped) {
      return false;
    }
    wait_for_gpu_progress(std::chrono::milliseconds{1});
  }
}

size_t acquire_frame_slot() {
  ZoneScopedN("Acquire frame slot");
  const auto waitStart = PresentClock::now();
  bool reported = false;
  while (true) {
    if (!reported && PresentClock::now() - waitStart >= std::chrono::seconds{2}) {
      Log.warn("Frame stalled: frame slots exhausted; render_idle={}", render_worker::is_idle());
      reported = true;
    }
    if (const auto slot = g_frameSlots.try_acquire()) {
      const auto waitDuration = PresentClock::now() - waitStart;
      const double waitMs = std::chrono::duration<double, std::milli>{waitDuration}.count();
      TracyPlot("aurora: frameSlotWaitMs", waitMs);
      return *slot;
    }
    wait_for_gpu_progress(std::chrono::microseconds{100});
  }
}

std::optional<size_t> acquire_mapped_staging_buffer() {
  ZoneScopedN("Acquire mapped staging buffer");
  const auto waitStart = PresentClock::now();
  bool reported = false;
  while (true) {
    if (!reported && PresentClock::now() - waitStart >= std::chrono::seconds{2}) {
      Log.warn("Frame stalled: staging slots exhausted; render_idle={}", render_worker::is_idle());
      reported = true;
    }
    if (auto slot = g_stagingSlots.try_acquire()) {
      if (wait_for_staging_buffer(*slot)) {
        return *slot;
      }
      g_stagingSlots.release(*slot);
      return std::nullopt;
    }
    wait_for_gpu_progress(std::chrono::microseconds{100});
  }
}

bool begin_frame() {
  ZoneScoped;
  // pace_frame_start();
  const size_t frameSlot = acquire_frame_slot();
  const auto stagingSlot = acquire_mapped_staging_buffer();
  if (!stagingSlot) {
    g_frameSlots.release(frameSlot);
    return false;
  }

  auto& frame = g_framePackets[frameSlot];
  frame = {};
  frame.frameId = g_nextFrameId++;
  frame.frameIndex = g_frameIndex;
  frame.stagingBuffer = *stagingSlot;
  frame.deferEncoding = g_queueStaging;
  size_t bufferOffset = 0;
  const auto& stagingBuf = g_stagingBuffers[*stagingSlot];
  const auto mapBuffer = [&](ByteBuffer& buf, uint64_t size) {
    if (size <= 0 || g_queueStaging) {
      return;
    }
    buf = ByteBuffer{static_cast<u8*>(stagingBuf.GetMappedRange(bufferOffset, size)), static_cast<size_t>(size)};
    bufferOffset += size;
  };
  mapBuffer(frame.verts, VertexBufferSize);
  mapBuffer(frame.uniforms, UniformBufferSize);
  mapBuffer(frame.indices, IndexBufferSize);
  mapBuffer(frame.storage, StorageBufferSize);
  if constexpr (UseTextureBuffer) {
    mapBuffer(frame.textureUpload, TextureUploadSize);
  }

  begin_recording(frame, frameSlot);
  begin_pipeline_frame();
  render_worker::enqueue_begin_frame(frame.frameId, [frameSlot] {
    constexpr wgpu::CommandEncoderDescriptor EncoderDescriptor{.label = "Redraw encoder"};
    g_framePackets[frameSlot].encoder = g_device.CreateCommandEncoder(&EncoderDescriptor);
    webgpu::gpu_prof::frame_begin(g_framePackets[frameSlot].encoder);
  });
  g_cpuFrameStart = PresentClock::now();
  return true;
}

void end_frame(EndFrameCallback callback) {
  ZoneScoped;
  if (g_cpuFrameStart.time_since_epoch().count() != 0) {
    const auto cpuFrameTime = PresentClock::now() - g_cpuFrameStart;
    update_ema(g_cpuFrameTimeNs, duration_ns(cpuFrameTime));
    const double cpuFrameTimeMs = std::chrono::duration<double, std::milli>{cpuFrameTime}.count();
    TracyPlot("aurora: cpuFrameTimeMs", cpuFrameTimeMs);
  }
  const auto recorded = end_recording();
  auto& frame = *recorded.packet;
  const size_t frameSlot = recorded.frameSlot;
  const uint64_t frameId = frame.frameId;
  end_pipeline_frame();
  ++g_frameIndex;

  const size_t stagingSlot = frame.stagingBuffer;
  render_worker::enqueue_end_frame(frameId, [frameSlot, stagingSlot, callback = std::move(callback)]() mutable {
    auto& packet = g_framePackets[frameSlot];
    const auto uploadStart = PresentClock::now();
    if (g_queueStaging) {
      const auto upload = [&](ByteBuffer& data, uint64_t offset, uint64_t capacity) {
        if (data.empty()) {
          return;
        }
        const auto size = data.size();
        const auto padded = staging_upload_size(size);
        CHECK(padded <= capacity, "Staging upload overflow: {} > {}", padded, capacity);
        data.reserve_extra(padded - size);
        std::memset(data.data() + size, 0, padded - size);
        g_queue.WriteBuffer(g_stagingBuffers[stagingSlot], offset, data.data(), padded);
      };
      upload(packet.verts, VertexStagingOffset, VertexBufferSize);
      upload(packet.uniforms, UniformStagingOffset, UniformBufferSize);
      upload(packet.indices, IndexStagingOffset, IndexBufferSize);
      upload(packet.storage, StorageStagingOffset, StorageBufferSize);
      if constexpr (UseTextureBuffer) {
        upload(packet.textureUpload, TextureUploadStagingOffset, TextureUploadSize);
      }
    } else {
      g_stagingBuffers[stagingSlot].Unmap();
    }
    [[maybe_unused]] const double uploadMs = std::chrono::duration<double, std::milli>(PresentClock::now() - uploadStart).count();
    TracyPlot("aurora: stagingUploadMs", uploadMs);
    g_mappingStates[stagingSlot].store(BufferMapState::Unmapped, std::memory_order_release);
    const auto encodeStart = PresentClock::now();
    [[maybe_unused]] const bool batched = encode_deferred_frame(packet.encoder, packet);
#ifdef __ANDROID__
    probe_uploaded_buffers(packet.encoder, packet);
#endif
    [[maybe_unused]] const double encodeMs =
        std::chrono::duration<double, std::milli>(PresentClock::now() - encodeStart).count();
    [[maybe_unused]] const auto opCount = packet.ops.size();
    auto encoder = std::move(packet.encoder);
    const auto stats = packet.stats;
    [[maybe_unused]] const bool textureOnlyUploads = packet.textureOnlyUploads;
    auto afterSubmitCallbacks = std::move(packet.afterSubmitCallbacks);
    packet = {};
    g_resources.stats.drawCallCount = stats.drawCallCount;
    g_resources.stats.mergedDrawCallCount = stats.mergedDrawCallCount;
    g_resources.stats.lastVertSize = stats.lastVertSize;
    g_resources.stats.lastUniformSize = stats.lastUniformSize;
    g_resources.stats.lastIndexSize = stats.lastIndexSize;
    g_resources.stats.lastStorageSize = stats.lastStorageSize;
    g_resources.stats.lastTextureUploadSize = stats.lastTextureUploadSize;
#ifdef __ANDROID__
    const auto submitStart = PresentClock::now();
#endif
    if (callback) {
      callback(encoder, std::move(afterSubmitCallbacks));
    }
#ifdef __ANDROID__
    const auto submitEnd = PresentClock::now();
    static auto lastPerformanceLog = PresentClock::time_point{};
    if (submitEnd - lastPerformanceLog >= std::chrono::seconds{5}) {
      lastPerformanceLog = submitEnd;
      const auto pipelines = pipeline_diagnostics();
      Log.info("Pipeline state: ready={} failed={} pending={} hits={} misses={}",
               pipelines.ready, pipelines.failed, pipelines.pending, pipelines.hits, pipelines.misses);
      Log.info("Performance: present_ms={:.2f} record_ms={:.2f} submit_ms={:.2f} draws={} merged={} "
               "vertex_bytes={} storage_bytes={} texture_upload_bytes={} vertex_path={} uniform_bytes={} "
               "upload_ms={:.2f} staging={} batched={} texture_only_uploads={} ops={} encode_ms={:.2f} "
               "acquire_ms={:.2f} finish_ms={:.2f} queue_submit_ms={:.2f} present_call_ms={:.2f}",
               static_cast<double>(g_presentPeriodNs.load(std::memory_order_acquire)) / 1'000'000.0,
               static_cast<double>(g_cpuFrameTimeNs.load(std::memory_order_acquire)) / 1'000'000.0,
               std::chrono::duration<double, std::milli>(submitEnd - submitStart).count(),
               stats.drawCallCount, stats.mergedDrawCallCount, stats.lastVertSize, stats.lastStorageSize,
               stats.lastTextureUploadSize, webgpu::g_textureVertexFetch ? "texture" : "storage",
               stats.lastUniformSize, uploadMs, g_queueStaging ? "queue" : "mapped", batched, textureOnlyUploads, opCount, encodeMs,
               g_resources.timings.acquireMs, g_resources.timings.finishMs,
               g_resources.timings.submitMs, g_resources.timings.presentMs);
    }
#endif
    g_frameSlots.release(frameSlot);
    expire_cached_bind_groups();
    map_staging_buffer(stagingSlot, true);
    process_events();
  });
}

uint32_t current_frame() noexcept { return g_frameIndex; }

void after_submit() noexcept { depth_peek::after_submit(); }

void gpu_synchronize() {
  if (!render_worker::is_worker_thread()) {
    gx::fifo::drain();
    flush_deferred_ops();
  }
  render_worker::synchronize();
}

void synchronize() { gpu_synchronize(); }

void after_present() noexcept {
  const auto now = PresentClock::now();
  const int64_t nowNs = timestamp_ns(now);
  const int64_t previousPresentNs = g_lastPresentNs.exchange(nowNs, std::memory_order_acq_rel);
  if (previousPresentNs != 0) {
    update_ema(g_presentPeriodNs, nowNs - previousPresentNs);
    const double presentPeriodMs = static_cast<double>(g_presentPeriodNs.load(std::memory_order_acquire)) / 1'000'000.0;
    TracyPlot("aurora: presentPeriodMs", presentPeriodMs);
  }
  std::lock_guard lock{g_presentStatsMutex};
  g_presentTimes.push_back(now);
  prune_present_times(now);
}

float calculate_fps() noexcept {
  const auto now = PresentClock::now();
  std::lock_guard lock{g_presentStatsMutex};
  prune_present_times(now);
  if (g_presentTimes.size() < 2) {
    return 0.f;
  }

  const auto elapsed = std::chrono::duration<float>(g_presentTimes.back() - g_presentTimes.front()).count();
  if (elapsed <= 0.f) {
    return 0.f;
  }
  return static_cast<float>(g_presentTimes.size() - 1) / elapsed;
}
} // namespace aurora::gfx

const AuroraStats* aurora_get_stats() { return &aurora::gfx::detail::resources().stats; }
float aurora_get_fps() { return aurora::gfx::calculate_fps(); }
