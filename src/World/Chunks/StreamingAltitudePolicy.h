#pragma once

namespace cutum
{

struct StreamingAltitudePolicyParams
{
  int AltitudeThresholdBlocks{32};
  int RenderDistancePenaltyPerChunk{1};
  float FogStartRatioBoost{0.15f};
};

struct StreamingAltitudeSnapshot
{
  int EffectiveRenderDistance{4};
  float EffectiveFogStartRatio{0.85f};
};

StreamingAltitudeSnapshot ComputeStreamingAltitude(
    int base_render_distance, float eye_y, float surface_y,
    float fog_start_ratio, const StreamingAltitudePolicyParams &params);

} // namespace cutum
