#include "WorldGen/Sampling/DensityFieldSampler.h"
#include "WorldGen/Core/Noise.h"
#include "WorldGen/Features/CaveCarver.h"
#include "WorldGen/Sampling/ClimateSampler.h"
#include <algorithm>
#include <cmath>

namespace cutum
{

namespace
{

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

float UDensityFieldSampler::SampleDensityAt(int x, int y, int z,
                                            int octaves) const
{
  const DensityRouteParams route = RouteAt(x, z);
  const float scale = Params.noiseScale;
  const float wx = static_cast<float>(x) * scale;
  const float wy = static_cast<float>(y) * scale;
  const float wz = static_cast<float>(z) * scale;
  const float noise3d =
      FBM3D(wx, wy, wz, Seed + 5100, octaves, Params.persistence,
            Params.lacunarity);
  float density = route.baseHeightBlocks - static_cast<float>(y) +
                  noise3d * route.noiseFactor * route.jaggedness;

  if (Params.cavesInDensity && y < MaxHeight - 4)
  {
    const int surface_y = SurfaceYAt(x, z);
    if (y <= surface_y - Caves.maxDepthBelowSurface && y >= Caves.minY)
    {
      const float cave_noise =
          CombinedCaveDensityNoise(x, y, z, Seed + 3000, Caves);
      density += cave_noise * Params.caveAmplitude;
    }
  }
  return density;
}

float UDensityFieldSampler::SampleDensity(int x, int y, int z) const
{
  return SampleDensityAt(x, y, z, Params.octaves);
}

int UDensityFieldSampler::SurfaceYAt(int x, int z) const
{
  int surface_y = SeaLevel;
  for (int y = MaxHeight; y >= 0; --y)
  {
    if (SampleDensityAt(x, y, z, Params.octaves) > 0.0f)
    {
      surface_y = y;
      break;
    }
  }
  return std::clamp(surface_y, 1, MaxHeight);
}

int UDensityFieldSampler::CoarseSurfaceYAt(int x, int z) const
{
  const int coarse_octaves = std::max(1, Params.octaves - 2);
  int surface_y = SeaLevel;
  for (int y = MaxHeight; y >= 0; --y)
  {
    if (SampleDensityAt(x, y, z, coarse_octaves) > 0.0f)
    {
      surface_y = y;
      break;
    }
  }
  return std::clamp(surface_y, 1, MaxHeight);
}

} // namespace cutum
