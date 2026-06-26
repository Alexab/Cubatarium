#include "World/Chunks/StreamingAltitudePolicy.h"

#include "World/Chunks/Chunk.h"
#include <algorithm>
#include <cmath>

namespace cutum
{

StreamingAltitudeSnapshot ComputeStreamingAltitude(
    int base_render_distance, float eye_y, float surface_y,
    float fog_start_ratio, const StreamingAltitudePolicyParams &params)
{
  StreamingAltitudeSnapshot snap;
  snap.EffectiveRenderDistance = std::max(1, base_render_distance);
  snap.EffectiveFogStartRatio = fog_start_ratio;

  const float altitude =
      std::max(0.0f, eye_y - surface_y - static_cast<float>(params.AltitudeThresholdBlocks));
  if (altitude <= 0.0f)
  {
    return snap;
  }

  const int penalty_chunks = static_cast<int>(
      altitude / static_cast<float>(CHUNK_SIZE * std::max(1, params.RenderDistancePenaltyPerChunk)));
  snap.EffectiveRenderDistance =
      std::max(1, base_render_distance - penalty_chunks);
  const float ratio_boost =
      params.FogStartRatioBoost *
      std::min(1.0f, altitude / static_cast<float>(CHUNK_SIZE * 4));
  snap.EffectiveFogStartRatio =
      std::max(0.35f, fog_start_ratio - ratio_boost);
  return snap;
}

} // namespace cutum
