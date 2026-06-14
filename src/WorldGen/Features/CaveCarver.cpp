#include "WorldGen/Features/CaveCarver.h"
#include "World/Core/BlockWorld.h"
#include "WorldGen/Core/Noise.h"
#include "WorldGen/Core/WorldGenContext.h"

namespace cutum
{

bool ShouldCarve(int x, int y, int z, int surfaceY, uint32_t seed,
                 const CaveParams &params)
{
  if (y < params.minY || y > surfaceY - params.maxDepthBelowSurface)
  {
    return false;
  }
  const float n = FBM3D(static_cast<float>(x) * params.scale,
                        static_cast<float>(y) * params.scale,
                        static_cast<float>(z) * params.scale, seed + 3000,
                        params.octaves, params.persistence, params.lacunarity);
  const float n01 = (n + 1.0f) * 0.5f;
  return n01 > params.threshold;
}

void CarveColumnCaves(WorldGenContext &ctx, int x, int z, int surfaceY,
                      uint32_t seed, const CaveParams &params)
{
  for (int y = params.minY; y <= surfaceY - params.maxDepthBelowSurface; ++y)
  {
    if (ShouldCarve(x, y, z, surfaceY, seed, params))
    {
      if (!ctx.World.IsAir(glm::ivec3(x, y, z)))
      {
        ctx.World.SetBlock(glm::ivec3(x, y, z), BLOCK_AIR);
      }
    }
  }
  ctx.MarkDirtyColumn(x, z, params.minY, surfaceY);
}

} // namespace cutum
