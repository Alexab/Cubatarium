#include "WorldGen/Sampling/DensityFieldSampler.h"
#include "WorldGen/Core/Noise.h"
#include "WorldGen/Features/CaveDepthBand.h"
#include "WorldGen/Features/CaveCarver.h"
#include "WorldGen/Features/RiverField.h"
#include "WorldGen/Sampling/ClimateSampler.h"
#include "WorldGen/Sampling/TerrainClimateMapper.h"
#include "WorldGen/Core/WorldGenPack.h"
#include <algorithm>
#include <cmath>

namespace cutum
{

namespace
{

constexpr float kSurfaceWobbleScale = 0.35f;

float CombinedCaveDensityNoise(int x, int y, int z, uint32_t seed,
                               const CaveParams &params)
{
  const float wx = static_cast<float>(x);
  const float wy = static_cast<float>(y);
  const float wz = static_cast<float>(z);
  const float cheese = FBM3D(wx * params.cheeseScale, wy * params.cheeseScale,
                               wz * params.cheeseScale, seed + 3100, 2,
                               params.persistence, params.lacunarity);
  const float spaghetti =
      FBM3D(wx * params.spaghettiScale, wy * params.spaghettiScale * 0.8f,
            wz * params.spaghettiScale, seed + 3200, 2, params.persistence,
            params.lacunarity);
  const float noodle =
      FBM3D(wx * params.noodleScale, wy * params.noodleScale * 1.2f,
            wz * params.noodleScale, seed + 3300, 2, params.persistence,
            params.lacunarity);
  return cheese * params.cheeseWeight + spaghetti * params.spaghettiWeight +
         noodle * params.noodleWeight;
}

float HorizontalSurfaceWobble(int x, int z, uint32_t seed, float scale,
                              int octaves, float persistence, float lacunarity,
                              float noise_factor, float jaggedness)
{
  const float horizontal = NormalizedFBM2D(
      static_cast<float>(x) * scale, static_cast<float>(z) * scale, seed + 5100,
      octaves, persistence, lacunarity);
  return (horizontal - 0.5f) * 2.0f * noise_factor * jaggedness *
         kSurfaceWobbleScale;
}

} // namespace

UDensityFieldSampler::UDensityFieldSampler(uint32_t seed, int seaLevel,
                                           int maxHeight, float terrainRoughness,
                                           const DensityFieldParams &params,
                                           const CaveParams &caves,
                                           int minHeight)
    : Seed(seed), SeaLevel(seaLevel), MaxHeight(maxHeight), MinHeight(minHeight),
      TerrainRoughness(terrainRoughness), Params(params), Caves(caves)
{
}

DensityRouteParams UDensityFieldSampler::RouteAt(int x, int z) const
{
  const bool v3 = Params.routerV3;
  float wx = static_cast<float>(x);
  float wz = static_cast<float>(z);
  if (v3)
  {
    // Domain warp: displaces the large-scale fields by up to ~22 blocks so
    // coastlines and ridges stop following the noise lattice.
    constexpr float kWarpScale = 0.0042f;
    constexpr float kWarpAmplitude = 22.0f;
    wx += kWarpAmplitude *
          SmoothFBM2D(static_cast<float>(x) * kWarpScale,
                      static_cast<float>(z) * kWarpScale, Seed + 5410, 2, 0.5f,
                      2.0f);
    wz += kWarpAmplitude *
          SmoothFBM2D(static_cast<float>(x) * kWarpScale + 71.0f,
                      static_cast<float>(z) * kWarpScale - 43.0f, Seed + 5411,
                      2, 0.5f, 2.0f);
  }
  const ClimateSample climate =
      SampleClimate(static_cast<int>(std::lround(wx)),
                    static_cast<int>(std::lround(wz)), Seed);
  DensityRouteParams route = RouteDensity(climate, SeaLevel, MaxHeight,
                                          MinHeight, TerrainRoughness, v3);

  const PackHeightConfig &height = UWorldGenPack::HeightConfig();
  const HeightLayerPackConfig regional =
      height.Loaded ? height.Regional : HeightLayerPackConfig{0.008f, 3, 0.32f};
  const HeightLayerPackConfig rolling =
      height.Loaded ? height.Rolling : HeightLayerPackConfig{0.012f, 3, 0.26f};
  const float land = Smoothstep(0.38f, 0.58f, climate.continentalness);
  const float lowErosion = 1.0f - Smoothstep(0.35f, 0.78f, climate.erosion);
  const float peakValleys = std::clamp(PeaksAndValleys(climate.weirdness), 0.0f, 1.0f);
  const float mountainWeight = MountainClimateWeight(climate);

  const float broadNoise =
      v3 ? 1.4f * SmoothFBM2D(wx * 0.0024f, wz * 0.0024f, Seed + 5401, 3, 0.5f,
                              2.0f)
         : FBM2D(wx * 0.0024f, wz * 0.0024f, Seed + 5401, 3, 0.5f, 2.0f);
  const float regionalNoise =
      v3 ? 1.4f * SmoothFBM2D(wx * regional.Scale, wz * regional.Scale,
                              Seed + 5402, std::max(1, regional.Octaves), 0.5f,
                              2.0f)
         : FBM2D(wx * regional.Scale, wz * regional.Scale, Seed + 5402,
                 std::max(1, regional.Octaves), 0.5f, 2.0f);
  const float rollingNoise =
      v3 ? 1.4f * SmoothFBM2D(wx * rolling.Scale, wz * rolling.Scale,
                              Seed + 5403, std::max(1, rolling.Octaves), 0.5f,
                              2.0f)
         : FBM2D(wx * rolling.Scale, wz * rolling.Scale, Seed + 5403,
                 std::max(1, rolling.Octaves), 0.5f, 2.0f);
  const float ridgeScaleBroad = std::max(0.0018f, regional.Scale * 0.32f);
  const float ridgeScaleFine = std::max(0.0035f, regional.Scale * 0.7f);
  const float broadRidgeNoise =
      v3 ? SmoothFBM2D(wx * ridgeScaleBroad, wz * ridgeScaleBroad, Seed + 5404,
                       3, 0.5f, 2.0f)
         : NormalizedFBM2D(wx * ridgeScaleBroad, wz * ridgeScaleBroad,
                           Seed + 5404, 3, 0.5f, 2.0f);
  const float fineRidgeNoise =
      v3 ? SmoothFBM2D(wx * ridgeScaleFine, wz * ridgeScaleFine, Seed + 5405, 3,
                       0.5f, 2.0f)
         : NormalizedFBM2D(wx * ridgeScaleFine, wz * ridgeScaleFine,
                           Seed + 5405, 3, 0.5f, 2.0f);
  const float broadRidged = 1.0f - std::fabs(broadRidgeNoise);
  const float fineRidged = 1.0f - std::fabs(fineRidgeNoise);
  const float ridged = broadRidged * (0.78f + 0.22f * fineRidged);
  const float verticalRange = static_cast<float>(MaxHeight - SeaLevel);
  const float roughness = std::max(0.25f, TerrainRoughness);

  float regionalAmplitude =
      verticalRange * (0.035f + 0.035f * roughness) * (0.35f + 0.65f * lowErosion);
  float rollingAmplitude =
      verticalRange * (0.015f + 0.018f * roughness) * (0.55f + 0.45f * lowErosion);
  float broadAmplitude = verticalRange * 0.055f;
  float mountainUplift = verticalRange * 0.42f;
  float mountainRidgeAmplitude = verticalRange * 0.52f;
  float mountainTrough = verticalRange * 0.05f;
  float mountainShape = mountainWeight;
  if (Params.routerV3)
  {
    // Bounded relief: plains/rolling terms shrink with erosion, the mountain
    // block uplift is capped (~0.54 of the vertical range at most instead of
    // ~0.94) and eased in with a smoothstep so foot slopes stay walkable.
    regionalAmplitude = verticalRange * 0.020f * (0.30f + 0.70f * lowErosion) *
                        (0.6f + 0.4f * roughness);
    rollingAmplitude = verticalRange * 0.008f * (0.40f + 0.60f * lowErosion) *
                       (0.6f + 0.4f * roughness);
    broadAmplitude = verticalRange * 0.040f;
    mountainUplift = verticalRange * 0.24f;
    mountainRidgeAmplitude = verticalRange * 0.30f;
    mountainTrough = verticalRange * 0.03f;
    mountainShape = mountainWeight * mountainWeight * (3.0f - 2.0f * mountainWeight);
  }
  route.baseHeightBlocks +=
      land * (broadNoise * broadAmplitude +
              regionalNoise * regionalAmplitude + rollingNoise * rollingAmplitude);
  route.baseHeightBlocks +=
      mountainShape *
      (mountainUplift +
       std::pow(std::clamp(ridged, 0.0f, 1.0f), 2.0f) *
           mountainRidgeAmplitude -
       mountainTrough);
  if (Params.riversEnabled && route.baseHeightBlocks > SeaLevel + 2 &&
      climate.continentalness > 0.40f)
  {
    const float channel = SampleRiverChannelStrength(
        x, z, Seed, Params.riverNoiseScale, Params.riverWidth,
        climate.erosion, climate.weirdness);
    const float wetness = Smoothstep(0.24f, 0.68f, climate.moisture);
    const float incision = channel * (0.2f + wetness * 0.8f);
    const float river_floor =
        std::max(static_cast<float>(MinHeight + 2),
                 static_cast<float>(SeaLevel - 2));
    float cut = std::max(0.0f, route.baseHeightBlocks - river_floor) * incision;
    if (Params.routerV3)
    {
      // Rivers are lowland features: fade the cut out with elevation and cap
      // its depth so a channel never becomes a canyon wall in highlands.
      const float above_sea = route.baseHeightBlocks - SeaLevel;
      const float lowland = 1.0f - Smoothstep(6.0f, 36.0f, above_sea);
      cut = std::min(cut * lowland, 6.0f);
    }
    route.baseHeightBlocks -= cut;
  }
  route.baseHeightBlocks =
      std::clamp(route.baseHeightBlocks, static_cast<float>(MinHeight),
                 static_cast<float>(MaxHeight));
  route.noiseFactor =
      (2.2f + roughness * 2.8f + mountainWeight * 4.0f) *
      ErosionAmplitudeMultiplier(climate.erosion);
  if (Params.routerV3)
  {
    // 3D noise is the "factor" of the router: tiny on lowlands, strong only
    // where the mountain weight is high.
    route.noiseFactor =
        (0.9f + roughness * 1.4f + mountainShape * 6.5f) *
        ErosionAmplitudeMultiplier(climate.erosion);
  }
  route.jaggedness = std::clamp(
      0.3f + (1.0f - climate.erosion) * 0.35f + mountainWeight * 0.65f +
          peakValleys * 0.15f,
      0.2f, 1.35f);
  return route;
}

float UDensityFieldSampler::HorizontalWobbleAt(
    int x, int z, const DensityRouteParams &route) const
{
  return HorizontalSurfaceWobble(
      x, z, Seed, Params.noiseScale, std::max(1, Params.octaves),
      Params.persistence, Params.lacunarity, route.noiseFactor,
      route.jaggedness);
}

float UDensityFieldSampler::SampleTerrainDensityAt(
    int x, int y, int z, int octaves, const DensityRouteParams &route,
    float horizontal_wobble) const
{
  const int effective_octaves =
      octaves > 0 ? octaves : std::max(1, Params.octaves);
  const float scale = Params.noiseScale;
  const float wx = static_cast<float>(x) * scale;
  const float wy = static_cast<float>(y) * scale;
  const float wz = static_cast<float>(z) * scale;
  const float terrain_noise =
      FBM3D(wx, wy, wz, Seed + 5200, effective_octaves, Params.persistence,
            Params.lacunarity);
  return route.baseHeightBlocks + route.noiseFactor * terrain_noise * 0.55f +
         horizontal_wobble - static_cast<float>(y);
}

float UDensityFieldSampler::ApplyCaveDensityOffset(int x, int y, int z,
                                                   int surface_y,
                                                   float density) const
{
  if (!Params.cavesInDensity || surface_y < Caves.minY)
  {
    return density;
  }
  const CaveDepthBand band = ComputeCaveDepthBand(surface_y, Caves);
  if (!band.valid || y < band.y_bottom || y > band.y_top)
  {
    return density;
  }
  const float cave_noise = CombinedCaveDensityNoise(x, y, z, Seed + 3000, Caves);
  return density + cave_noise * Params.caveAmplitude;
}

float UDensityFieldSampler::SampleTerrainDensity(int x, int y, int z,
                                                 int octaves) const
{
  const DensityRouteParams route = RouteAt(x, z);
  return SampleTerrainDensityAt(x, y, z, octaves, route,
                                 HorizontalWobbleAt(x, z, route));
}

float UDensityFieldSampler::SampleDensity(int x, int y, int z,
                                          int surface_y) const
{
  return SampleDensity(PrepareColumn(x, z, surface_y, surface_y), y);
}

UDensityFieldSampler::Column UDensityFieldSampler::PrepareColumn(
    int x, int z, int target_surface_y, int raw_surface_y) const
{
  Column column;
  column.x = x;
  column.z = z;
  column.surfaceY = target_surface_y;
  // Translate the complete density column so its raw zero crossing lands on
  // the biome-refined surface. This keeps the 3D caves and terrain noise while
  // making the voxel surface match the same smoothed height used by features.
  column.surfaceDensityOffset =
      static_cast<float>(target_surface_y - raw_surface_y);
  column.route = RouteAt(x, z);
  column.horizontalWobble = HorizontalWobbleAt(x, z, column.route);
  return column;
}

float UDensityFieldSampler::SampleDensity(const Column &column, int y) const
{
  float density = SampleTerrainDensityAt(column.x, y, column.z, Params.octaves,
                                         column.route,
                                         column.horizontalWobble) +
                  column.surfaceDensityOffset;
  return ApplyCaveDensityOffset(column.x, y, column.z, column.surfaceY, density);
}

int UDensityFieldSampler::SurfaceYAt(int x, int z) const
{
  const DensityRouteParams route = RouteAt(x, z);
  const float wobble = HorizontalWobbleAt(x, z, route);
  int terrain_surface = MinHeight;
  for (int y = MaxHeight; y >= MinHeight; --y)
  {
    if (SampleTerrainDensityAt(x, y, z, Params.octaves, route, wobble) > 0.0f)
    {
      terrain_surface = y;
      break;
    }
  }
  if (!Params.cavesInDensity)
  {
    return terrain_surface;
  }

  const Column column =
      PrepareColumn(x, z, terrain_surface, terrain_surface);
  for (int y = terrain_surface; y >= MinHeight; --y)
  {
    if (SampleDensity(column, y) > 0.0f)
    {
      return y;
    }
  }
  return std::clamp(static_cast<int>(std::lround(route.baseHeightBlocks)), MinHeight,
                    MaxHeight);
}

int UDensityFieldSampler::CoarseSurfaceYAt(int x, int z) const
{
  const DensityRouteParams route = RouteAt(x, z);
  const float wobble = HorizontalWobbleAt(x, z, route);
  const int coarse_octaves = std::max(1, Params.octaves - 2);
  for (int y = MaxHeight; y >= MinHeight; y -= 2)
  {
    if (SampleTerrainDensityAt(x, y, z, coarse_octaves, route, wobble) > 0.0f)
    {
      return y;
    }
  }
  return std::clamp(static_cast<int>(std::lround(route.baseHeightBlocks)), MinHeight,
                    MaxHeight);
}

} // namespace cutum
