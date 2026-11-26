#pragma once
#include "Render/Lighting/BoundSectionLightCapture.h"
#include "Render/Lighting/SectionRenderLightBinding.h"
#include "World/Core/World.h"
#include "World/Chunks/ChunkManager.h"
#include <map>
#include <tuple>
#include <optional>
#include <unordered_map>

namespace cutum::detail {
struct GpuOpaqueDrawStateWitness
{
  bool valid{false};
  bool submitted{false};
  uint64_t frame_epoch{0};
  uint8_t path{0};
  glm::ivec3 chunk_coord{0};
  int32_t block_id{-1};
  uint32_t batch_index{0};
  uint32_t command_slot{0};
  uint32_t indirect_buffer{0};
  uint8_t depth_test{0};
  uint8_t depth_write{0};
  uint8_t cull{0};
  uint8_t scissor{0};
  uint8_t stencil{0};
  uint8_t rasterizer_discard{0};
  uint8_t polygon_offset{0};
  uint8_t blend{0};
  uint8_t color_mask{0};
  int32_t depth_func{0};
  int32_t cull_mode{0};
  int32_t front_face{0};
  std::array<int32_t, 4> viewport{};
  std::array<int32_t, 4> scissor_box{};
  int32_t framebuffer{0};
  int32_t program{0};
  int32_t vao{0};
  int32_t array_buffer{0};
  int32_t element_buffer{0};
  int32_t texture_2d{0};
  uint8_t vertex_layout_checked{0};
  uint8_t vertex_layout_match{0};
  uint8_t mvp_matrix_valid{0};
  uint8_t texture_alpha_valid{0};
  uint8_t texture_alpha_min{0};
  uint8_t texture_alpha_max{0};
  uint8_t alpha_cutout{0};
  uint8_t opaque_depth_guard{0};
  int32_t greedy_mode{0};
  int32_t anim_frame{0};
  int32_t anim_frame_count{0};
  int32_t active_texture{0};
  float mvp_matrix_max_delta{-1.0f};
  int32_t stencil_func{0};
  int32_t stencil_ref{0};
  int32_t stencil_value_mask{0};
  int32_t stencil_write_mask{0};
  int32_t polygon_mode_front{0};
  int32_t polygon_mode_back{0};
  float depth_range_near{0.0f};
  float depth_range_far{1.0f};
  float polygon_offset_factor{0.0f};
  float polygon_offset_units{0.0f};
};

using CpuBoundLightKey=std::tuple<int,int,int,uint16_t,BlockId>;
using CpuBoundLightWitnesses=std::map<CpuBoundLightKey,std::pair<std::shared_ptr<SectionRenderLightBinding>,std::shared_ptr<const BoundSectionLightCapture>>>;
struct LightPreviewState {
  bool shown{false},raw{false};double last_change{-1e9},pending_since{-1.0},fade_start{-1.0};
};
using LightPreviewStates=std::unordered_map<glm::ivec3,LightPreviewState,IVec3Hash>;
// These are the actual source cells. Lazy optional parents let a fresh query
// read an empty owner without constructing map sentinels or scratch contents.
struct RenderThreadState {
  std::optional<CpuBoundLightWitnesses> Cpu;
  std::vector<GpuOpaqueDrawStateWitness> GpuFrames;
  std::optional<LightPreviewStates> Preview;
  std::optional<UWorld::EnvironmentState> EnvironmentValue;
  GpuOpaqueDrawStateWitness GpuCurrent;
  bool DiagnosticCenterPixelProbe{},CaptureGpu{},CaptureFluid{};
  uint8_t FluidPassOrdinal{255};
  uint64_t PreviewEpoch{UINT64_MAX},EnvironmentEpoch{UINT64_MAX};
  const UWorld *EnvironmentOwner{};
  std::array<uint64_t,3> FluidEpochs{UINT64_MAX,UINT64_MAX,UINT64_MAX};
  uint64_t TransparentEpoch{UINT64_MAX};uint8_t TransparentOrdinal{};
  CpuBoundLightWitnesses &CpuWitnesses(){if(!Cpu)Cpu.emplace();return *Cpu;}
  LightPreviewStates &PreviewStates(){if(!Preview)Preview.emplace();return *Preview;}
  UWorld::EnvironmentState &Environment(){if(!EnvironmentValue)EnvironmentValue.emplace();return *EnvironmentValue;}
};
inline RenderThreadState &CallingRenderThreadState() noexcept {thread_local RenderThreadState state;return state;}
} // namespace cutum::detail
