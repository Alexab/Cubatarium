#include "WorldGen/Sampling/TerrainClimateMapper.h"
#include "WorldGen/Core/Noise.h"
#include <algorithm>
#include <cmath>

namespace cutum
{

namespace
{

float CatmullRom1D(float t, float p0, float p1, float p2, float p3)
{
  const float t2 = t * t;
  const float t3 = t2 * t;
  return 0.5f * ((2.0f * p1) + (-p0 + p2) * t +
                   (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                   (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

float SplineHeightFromContinental(float c, int seaLevel, int maxHeight)
{
  const float range = static_cast<float>(std::max(8, maxHeight - seaLevel));
  const float oceanDeep = seaLevel - range * 0.30f;
  const float oceanShallow = seaLevel - range * 0.12f;
  const float beach = seaLevel - range * 0.03f;
  const float inland = seaLevel + range * 0.04f;
  const float highland = seaLevel + range * 0.18f;

  if (c < 0.28f)
  {
    const float t = c / 0.28f;
    return oceanDeep + (oceanShallow - oceanDeep) * t;
  }
  if (c < 0.38f)
  {
    const float t = (c - 0.28f) / 0.10f;
    return oceanShallow + (beach - oceanShallow) * t;
  }
  if (c < 0.55f)
  {
    const float t = (c - 0.38f) / 0.17f;
    return beach + (inland - beach) * t;
  }
  const float t = std::clamp((c - 0.55f) / 0.45f, 0.0f, 1.0f);
  return inland + (highland - inland) * CatmullRom1D(t, inland, inland, highland,
                                                     seaLevel + range * 0.55f);
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
      erosion_mul * (erosion_flat + low_erosion_boost * 0.35f) * 0.30f;
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
    rolling_delta = (rolling01 - 0.5f) * 2.0f * amplitudeBlocks *
                    rollingWeight * (erosion_flat + low_erosion_boost * 0.5f);
  }

  const float climate_base =
      ContinentalHeightBlocks(climate.continentalness, seaLevel, maxHeight);
  const float mid_bias = static_cast<float>(seaLevel) + amplitudeBlocks * 0.15f;
  return (climate_base - mid_bias) + regional_delta + detail_delta + ridge_boost +
         rolling_delta;
}

} // namespace cutum
