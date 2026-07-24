#include "WorldGen/Sampling/DensityFieldSampler.h"
#include "WorldGen/Core/Noise.h"
#include "WorldGen/Features/CaveDepthBand.h"
#include "WorldGen/Features/CaveCarver.h"
#include "WorldGen/Sampling/ClimateSampler.h"
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
                                           const CaveParams &caves)
    : Seed(seed), SeaLevel(seaLevel), MaxHeight(maxHeight),
      TerrainRoughness(terrainRoughness), Params(params), Caves(caves)
{
}

DensityRouteParams UDensityFieldSampler::RouteAt(int x, int z) const
{
  const ClimateSample climate = SampleClimate(x, z, Seed);
  return RouteDensity(climate, SeaLevel, MaxHeight, TerrainRoughness);
}

float UDensityFieldSampler::SampleTerrainDensityAt(
    int x, int y, int z, int octaves, const DensityRouteParams &route) const
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
  const float wobble = HorizontalSurfaceWobble(
      x, z, Seed, Params.noiseScale, effective_octaves, Params.persistence,
      Params.lacunarity, route.noiseFactor, route.jaggedness);
  return route.baseHeightBlocks + route.noiseFactor * terrain_noise * 0.55f +
         wobble - static_cast<float>(y);
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
  return SampleTerrainDensityAt(x, y, z, octaves, RouteAt(x, z));
}

float UDensityFieldSampler::SampleDensity(int x, int y, int z,
                                          int surface_y) const
{
  const DensityRouteParams route = RouteAt(x, z);
  float density =
      SampleTerrainDensityAt(x, y, z, Params.octaves, route);
  return ApplyCaveDensityOffset(x, y, z, surface_y, density);
}

int UDensityFieldSampler::SurfaceYAt(int x, int z) const
{
  const DensityRouteParams route = RouteAt(x, z);
  for (int y = MaxHeight; y >= 1; --y)
  {
    if (SampleTerrainDensityAt(x, y, z, Params.octaves, route) > 0.0f)
    {
      return y;
    }
  }
  return std::clamp(static_cast<int>(std::lround(route.baseHeightBlocks)), 1,
                    MaxHeight);
}

int UDensityFieldSampler::CoarseSurfaceYAt(int x, int z) const
{
  const DensityRouteParams route = RouteAt(x, z);
  const int coarse_octaves = std::max(1, Params.octaves - 2);
  for (int y = MaxHeight; y >= 1; y -= 2)
  {
    if (SampleTerrainDensityAt(x, y, z, coarse_octaves, route) > 0.0f)
    {
      return y;
    }
  }
  return std::clamp(static_cast<int>(std::lround(route.baseHeightBlocks)), 1,
                    MaxHeight);
}

} // namespace cutum
