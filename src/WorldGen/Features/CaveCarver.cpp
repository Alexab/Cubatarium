#include "WorldGen/Features/CaveCarver.h"
#include "World/Core/BlockWorld.h"
#include "WorldGen/Core/Noise.h"
#include "WorldGen/Core/WorldGenContext.h"
#include <cmath>

namespace cutum
{

bool ShouldCarve(int x, int y, int z, int surfaceY, uint32_t Seed,
                 const CaveParams &params)
{
  if (y < params.minY || y > surfaceY - params.maxDepthBelowSurface)
  {
    return false;
  }
  if (params.useDensityField)
  {
    float density = static_cast<float>(surfaceY - y);
    const float caveNoise = FBM3D(static_cast<float>(x) * params.scale,
                                  static_cast<float>(y) * params.scale,
                                  static_cast<float>(z) * params.scale,
                                  Seed + 3000, params.octaves, params.persistence,
                                  params.lacunarity);
    density += caveNoise * params.densityCaveAmplitude * 24.0f;
    return density < 0.0f;
  }
  const float n = FBM3D(static_cast<float>(x) * params.scale,
                        static_cast<float>(y) * params.scale,
                        static_cast<float>(z) * params.scale, Seed + 3000,
                        params.octaves, params.persistence, params.lacunarity);
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
  const int yMax = surfaceY - params.maxDepthBelowSurface;
  if (yMax < params.minY)
  {
    return;
  }
  for (int y = params.minY; y <= yMax; ++y)
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
  ctx.AccumulateDirtyColumn(params.minY, surfaceY);
}

} // namespace cutum
