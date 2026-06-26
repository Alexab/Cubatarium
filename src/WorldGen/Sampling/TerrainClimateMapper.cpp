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
                           float amplitudeBlocks, float terrainRoughness)
{
  const float erosion = std::clamp(climate.erosion, 0.0f, 1.0f);
  const float erosionMul = ErosionAmplitudeMultiplier(erosion);
  const float erosionFlat = 1.0f - erosion;
  const float pv = PeaksAndValleys(climate.weirdness);

  const float regionalDelta =
      (regionalNoise01 - 0.5f) * 2.0f * amplitudeBlocks * terrainRoughness *
      erosionMul * erosionFlat * 0.30f;
  const float detailDelta = (detailNoise01 - 0.5f) * 2.0f * detailWeight *
                            amplitudeBlocks * erosionFlat * erosionFlat * 0.40f;
  const float ridgeBoost =
      (pv - 0.5f) * amplitudeBlocks * 0.20f * erosionFlat;

  const float climateBase =
      ContinentalHeightBlocks(climate.continentalness, seaLevel, maxHeight);
  const float midBias = static_cast<float>(seaLevel) + amplitudeBlocks * 0.15f;
  return (climateBase - midBias) + regionalDelta + detailDelta + ridgeBoost;
}

} // namespace cutum
