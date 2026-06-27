#include "Render/Engine/DistanceFog.h"

#include "World/Chunks/Chunk.h"

#include <algorithm>

namespace cutum
{

namespace
{
constexpr float kMinFogStartBlocks = 16.0f;
constexpr float kHorizonMarginBlocks = 24.0f;
constexpr float kMinHorizonBlocks = 32.0f;
} // namespace

float RenderHorizonBlocks(int render_distance_chunks)
{
  return static_cast<float>(std::max(1, render_distance_chunks)) *
         static_cast<float>(CHUNK_SIZE);
}

float FogHorizonBlocks(int render_distance_chunks)
{
  const float full = RenderHorizonBlocks(render_distance_chunks);
  return std::max(kMinHorizonBlocks, full - kHorizonMarginBlocks);
}

float StreamingHorizonBlocks(int render_distance_chunks)
{
  return FogHorizonBlocks(render_distance_chunks);
}

DistanceFogParams ComputeDistanceFog(int render_distance_chunks,
                                     glm::vec3 sky_color, float start_ratio,
                                     float effective_fog_start_ratio,
                                     float fog_density)
{
  const float ratio = effective_fog_start_ratio >= 0.0f
                          ? effective_fog_start_ratio
                          : start_ratio;
  const float fog_blocks = FogHorizonBlocks(render_distance_chunks);
  const float clamped_ratio = std::clamp(ratio, 0.0f, 1.0f);
  DistanceFogParams params;
  params.End = fog_blocks;
  params.Start = std::max(fog_blocks * clamped_ratio, kMinFogStartBlocks);
  if (params.Start >= params.End)
  {
    params.Start = params.End * 0.85f;
  }
  params.Density = std::max(0.1f, fog_density);
  params.Color = sky_color;
  return params;
}

} // namespace cutum
