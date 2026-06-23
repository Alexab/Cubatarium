#include "Render/Engine/DistanceFog.h"

#include "World/Chunks/Chunk.h"

#include <algorithm>

namespace cutum
{

namespace
{
constexpr float kMinFogStartBlocks = 32.0f;
}

DistanceFogParams ComputeDistanceFog(int render_distance_chunks,
                                     glm::vec3 sky_color, float start_ratio)
{
  const float render_blocks =
      static_cast<float>(std::max(1, render_distance_chunks)) *
      static_cast<float>(CHUNK_SIZE);
  const float clamped_ratio = std::clamp(start_ratio, 0.0f, 1.0f);
  DistanceFogParams params;
  params.End = render_blocks;
  params.Start = std::max(render_blocks * clamped_ratio, kMinFogStartBlocks);
  if (params.Start >= params.End)
  {
    params.Start = params.End * 0.85f;
  }
  params.Color = sky_color;
  return params;
}

} // namespace cutum
