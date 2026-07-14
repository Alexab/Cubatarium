#include "WorldGen/Sampling/TerrainClimateMapper.h"
#include "WorldGen/Core/Noise.h"
#include <algorithm>
#include <cmath>

namespace cutum
{

namespace
{

float SplineHeightFromContinental(float c, int seaLevel, int maxHeight)
{
  const float range = static_cast<float>(std::max(8, maxHeight - seaLevel));
  const float u = std::clamp(c, 0.0f, 1.0f);

  const float y_deep = seaLevel - range * 0.20f;
  const float y_shallow = seaLevel - range * 0.06f;
  const float y_coast = seaLevel + range * 0.01f;
  const float y_low = seaLevel + range * 0.08f;
  const float y_hills = seaLevel + range * 0.16f;
  const float y_high = seaLevel + range * 0.24f;

  if (u < 0.25f)
  {
    const float t = u / 0.25f;
    return y_deep + (y_shallow - y_deep) * Smoothstep(0.0f, 1.0f, t);
  }
  if (u < 0.50f)
  {
    const float t = (u - 0.25f) / 0.25f;
    return y_shallow + (y_coast - y_shallow) * Smoothstep(0.0f, 1.0f, t);
  }
  if (u < 0.72f)
  {
    const float t = (u - 0.50f) / 0.22f;
    return y_coast + (y_hills - y_coast) * Smoothstep(0.0f, 1.0f, t);
  }
  const float t = (u - 0.72f) / 0.28f;
  return y_hills + (y_high - y_hills) * Smoothstep(0.0f, 1.0f, t);
}

} // namespace

float PeaksAndValleys(float weirdness)
{
  const float w = std::clamp(weirdness, 0.0f, 1.0f);
  return 1.0f - std::fabs(3.0f * std::fabs(w * 2.0f - 1.0f) - 2.0f);
}

float ContinentalHeightBlocks(float continentalness, int seaLevel, int maxHeight)
{
  return SplineHeightFromContinental(std::clamp(continentalness, 0.0f, 1.0f),
                                     seaLevel, maxHeight);
}

float ErosionAmplitudeMultiplier(float erosion)
{
  const float e = std::clamp(erosion, 0.0f, 1.0f);
  return 1.2f + (0.25f - 1.2f) * Smoothstep(0.2f, 0.75f, e);
}

float ClimateTerrainOffset(const ClimateSample &climate, int seaLevel,
                           int maxHeight, float regionalNoise01,
                           float detailNoise01, float detailWeight,
                           float amplitudeBlocks, float terrainRoughness,
                           int worldX, int worldZ, uint32_t seed,
                           float rollingWeight, float rollingScale,
                           int rollingOctaves)
{
  const float erosion = std::clamp(climate.erosion, 0.0f, 1.0f);
  const float erosion_mul = ErosionAmplitudeMultiplier(erosion);
  const float erosion_flat = 1.0f - erosion;
  const float low_erosion_boost = Smoothstep(0.35f, 0.0f, erosion);
  const float pv = PeaksAndValleys(climate.weirdness);

  const float regional_delta =
      (regionalNoise01 - 0.5f) * 2.0f * amplitudeBlocks * terrainRoughness *
      erosion_mul * (erosion_flat + low_erosion_boost * 0.35f) * 0.42f;
  const float detail_delta =
      (detailNoise01 - 0.5f) * 2.0f * detailWeight * amplitudeBlocks *
      (erosion_flat * erosion_flat + low_erosion_boost * 0.25f) * 0.40f;
  const float ridge_boost =
      (pv - 0.5f) * amplitudeBlocks * 0.20f * erosion_flat;

  float rolling_delta = 0.0f;
  if (rollingWeight > 0.0f && rollingOctaves > 0)
  {
    const float rolling01 =
        (NormalizedFBM2D(static_cast<float>(worldX) * rollingScale,
                         static_cast<float>(worldZ) * rollingScale,
                         seed + 4400, rollingOctaves, 0.5f, 2.0f) +
         1.0f) *
        0.5f;
    rolling_delta = (rolling01 - 0.5f) * 2.0f * amplitudeBlocks * 1.55f *
                    rollingWeight * (erosion_flat + low_erosion_boost * 0.5f);
  }

  const float climate_base =
      ContinentalHeightBlocks(climate.continentalness, seaLevel, maxHeight);
  const float mid_bias = static_cast<float>(seaLevel) + amplitudeBlocks * 0.12f;
  return (climate_base - mid_bias) + regional_delta + detail_delta + ridge_boost +
         rolling_delta;
}

} // namespace cutum
