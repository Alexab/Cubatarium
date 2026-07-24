#include "WorldGen/Features/CaveCarver.h"
#include "WorldGen/Features/CaveDepthBand.h"
#include "World/Core/BlockWorld.h"
#include "WorldGen/Core/Noise.h"
#include "WorldGen/Core/WorldGenContext.h"
#include <cmath>

namespace cutum
{

namespace
{

float CombinedCaveNoise(int x, int y, int z, uint32_t seed, const CaveParams &params)
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

bool ShouldCarve(int x, int y, int z, int surfaceY, uint32_t Seed,
                 const CaveParams &params)
{
  const CaveDepthBand band = ComputeCaveDepthBand(surfaceY, params);
  if (!band.valid || y < band.y_bottom || y > band.y_top)
  {
    return false;
  }
  if (params.useDensityField)
  {
    float density = static_cast<float>(surfaceY - y);
    const float cave_noise = CombinedCaveNoise(x, y, z, Seed + 3000, params);
    density += cave_noise * params.densityCaveAmplitude * 24.0f;
    return density < 0.0f;
  }
  const float n = CombinedCaveNoise(x, y, z, Seed + 3000, params);
  const float n01 = (n + 1.0f) * 0.5f;
  return n01 > params.threshold;
}

bool ShouldCarveWorm(int x, int y, int z, int surfaceY, uint32_t Seed)
{
  if (y < 4 || y > surfaceY - 2)
  {
    return false;
  }
  const int wx = (x >> 4) * 16;
  const int wz = (z >> 4) * 16;
  const uint32_t cellSeed = Seed ^ static_cast<uint32_t>(wx * 92837111 + wz);
  const float ax =
      FBM2D(static_cast<float>(wx) * 0.03f, static_cast<float>(wz) * 0.03f,
            cellSeed, 2, 0.5f, 2.0f) * 8.0f;
  const float az =
      FBM2D(static_cast<float>(wx) * 0.03f + 17.0f,
            static_cast<float>(wz) * 0.03f + 31.0f, cellSeed + 1, 2, 0.5f,
            2.0f) *
      8.0f;
  const float tunnelY =
      static_cast<float>(surfaceY) * 0.45f +
      FBM2D(static_cast<float>(wx) * 0.01f, static_cast<float>(wz) * 0.01f,
            cellSeed + 2, 2, 0.5f, 2.0f) *
          6.0f;
  const float dx = static_cast<float>(x - wx) - 8.0f - ax;
  const float dz = static_cast<float>(z - wz) - 8.0f - az;
  const float dy = static_cast<float>(y) - tunnelY;
  return (dx * dx + dy * dy * 2.0f + dz * dz) < 6.0f;
}

void CarveColumnCaves(WorldGenContext &ctx, int x, int z, int surfaceY,
                      uint32_t Seed, const CaveParams &params)
{
  const CaveDepthBand band = ComputeCaveDepthBand(surfaceY, params);
  if (!band.valid)
  {
    return;
  }
  for (int y = band.y_bottom; y <= band.y_top; ++y)
  {
    const bool carve = params.style == CaveStyle::Worm
                           ? ShouldCarveWorm(x, y, z, surfaceY, Seed)
                           : ShouldCarve(x, y, z, surfaceY, Seed, params);
    if (!carve)
    {
      continue;
    }
    const glm::ivec3 pos(x, y, z);
    if (!ctx.World.IsAir(pos))
    {
      ctx.World.SetBlock(pos, BLOCK_AIR);
    }
  }
  ctx.AccumulateDirtyColumn(band.y_bottom, surfaceY);
}

} // namespace cutum
