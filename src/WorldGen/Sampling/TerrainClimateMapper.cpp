#include "WorldGen/Sampling/TerrainClimateMapper.h"
#include "WorldGen/Core/Noise.h"
#include "WorldGen/Core/WorldGenPack.h"
#include <algorithm>
#include <cmath>

namespace cutum
{

namespace
{

float SplineHeightFromContinental(float c, int seaLevel, int maxHeight,
                                  int minHeight, bool gentleShoreline)
{
  const float landRange =
      static_cast<float>(std::max(8, maxHeight - seaLevel));
  const float u = std::clamp(c, 0.0f, 1.0f);

  const PackHeightConfig &height_config = UWorldGenPack::HeightConfig();
  const PackHeightConfig::OceanProfile &profile = height_config.Ocean;
  const bool signed_ocean_profile = height_config.SchemaVersion >= 2;
  const float oceanRange = static_cast<float>(std::max(
      8, signed_ocean_profile ? seaLevel - minHeight : maxHeight - seaLevel));
  const float y_deep = seaLevel - oceanRange * profile.DeepOceanDepthRatio;
  const float y_shelf = seaLevel - oceanRange * profile.ShelfDepthRatio;
  const float y_coast = seaLevel - oceanRange * profile.CoastDepthRatio;
  const float y_lowland = seaLevel + landRange * profile.LowlandHeightRatio;
  const float y_hills = seaLevel + landRange * profile.HillHeightRatio;
  const float y_highland = seaLevel + landRange * profile.HighlandHeightRatio;

  if (gentleShoreline)
  {
    // Profile 3: a broad, shallow shelf so the waterline is crossed on a
    // gentle slope (Minecraft beaches), with the steep part kept offshore.
    const float y_near = seaLevel - oceanRange * 0.12f;
    const float y_wade = seaLevel - 4.0f;
    const float y_beach = seaLevel + 1.5f;
    struct Node
    {
      float u;
      float y;
    };
    const Node nodes[] = {{0.0f, y_deep},
                          {profile.DeepOceanEnd, y_shelf},
                          {profile.ShelfEnd, y_near},
                          {0.56f, y_wade},
                          {0.61f, y_beach},
                          {profile.CoastEnd, y_lowland},
                          {0.90f, y_hills},
                          {1.0f, y_highland}};
    for (size_t i = 1; i < sizeof(nodes) / sizeof(nodes[0]); ++i)
    {
      if (u <= nodes[i].u || i + 1 == sizeof(nodes) / sizeof(nodes[0]))
      {
        const float span = std::max(0.0001f, nodes[i].u - nodes[i - 1].u);
        const float t = std::clamp((u - nodes[i - 1].u) / span, 0.0f, 1.0f);
        return nodes[i - 1].y +
               (nodes[i].y - nodes[i - 1].y) * Smoothstep(0.0f, 1.0f, t);
      }
    }
    return y_highland;
  }
  if (u < profile.DeepOceanEnd)
  {
    const float t = u / profile.DeepOceanEnd;
    return y_deep + (y_shelf - y_deep) * Smoothstep(0.0f, 1.0f, t);
  }
  if (u < profile.ShelfEnd)
  {
    const float t = (u - profile.DeepOceanEnd) /
                    (profile.ShelfEnd - profile.DeepOceanEnd);
    return y_shelf + (y_coast - y_shelf) * Smoothstep(0.0f, 1.0f, t);
  }
  if (u < profile.CoastEnd)
  {
    const float t = (u - profile.ShelfEnd) /
                    (profile.CoastEnd - profile.ShelfEnd);
    return y_coast + (y_lowland - y_coast) * Smoothstep(0.0f, 1.0f, t);
  }
  if (u < 0.90f)
  {
    const float t = (u - profile.CoastEnd) / (0.90f - profile.CoastEnd);
    return y_lowland + (y_hills - y_lowland) * Smoothstep(0.0f, 1.0f, t);
  }
  const float t = (u - 0.90f) / 0.10f;
  return y_hills + (y_highland - y_hills) * Smoothstep(0.0f, 1.0f, t);
}

} // namespace

float PeaksAndValleys(float weirdness)
{
  const float w = std::clamp(weirdness, 0.0f, 1.0f);
  return 1.0f - std::fabs(3.0f * std::fabs(w * 2.0f - 1.0f) - 2.0f);
}

float MountainClimateWeight(const ClimateSample &climate)
{
  const float land = Smoothstep(0.38f, 0.58f, climate.continentalness);
  const float inland = Smoothstep(0.50f, 0.82f, climate.continentalness);
  const float lowErosion =
      1.0f - Smoothstep(0.35f, 0.78f, climate.erosion);
  const float peakValleys =
      std::clamp(PeaksAndValleys(climate.weirdness), 0.0f, 1.0f);
  return land * inland * (0.30f + 0.70f * lowErosion) *
         Smoothstep(0.28f, 0.65f, climate.ridge) *
         (0.80f + 0.20f * Smoothstep(0.12f, 0.65f, peakValleys));
}

float ContinentalHeightBlocks(float continentalness, int seaLevel,
                              int maxHeight, int minHeight,
                              bool gentleShoreline)
{
  return SplineHeightFromContinental(std::clamp(continentalness, 0.0f, 1.0f),
                                     seaLevel, maxHeight, minHeight,
                                     gentleShoreline);
}

float ErosionAmplitudeMultiplier(float erosion)
{
  const float e = std::clamp(erosion, 0.0f, 1.0f);
  return 1.2f + (0.25f - 1.2f) * Smoothstep(0.2f, 0.75f, e);
}

float ClimateTerrainOffset(const ClimateSample &climate, int seaLevel,
                           int maxHeight, int minHeight,
                           float regionalNoise01,
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
      ContinentalHeightBlocks(climate.continentalness, seaLevel, maxHeight,
                              minHeight);
  const float mid_bias = static_cast<float>(seaLevel) + amplitudeBlocks * 0.12f;
  return (climate_base - mid_bias) + regional_delta + detail_delta + ridge_boost +
         rolling_delta;
}

} // namespace cutum
