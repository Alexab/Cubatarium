#include "WorldGen/Features/RavineCarver.h"
#include "World/Core/BlockWorld.h"
#include "WorldGen/Core/Noise.h"
#include "WorldGen/Core/WorldGenContext.h"
#include <algorithm>
#include <cmath>

namespace cutum
{

namespace
{

uint32_t RavineHash(int x, int z, uint32_t seed)
{
  return static_cast<uint32_t>(x * 374761393 + z * 668265263) ^ (seed + 4400);
}

} // namespace

void CarveColumnRavines(WorldGenContext &ctx, int x, int z, int surfaceY,
                        uint32_t seed, const RavineParams &params)
{
  if (!params.enabled || params.rarity <= 0)
  {
    return;
  }
  if (RavineHash(x, z, seed) % static_cast<uint32_t>(params.rarity) != 0)
  {
    return;
  }

  const float ridge =
      1.0f -
      std::fabs(NormalizedFBM2D(static_cast<float>(x) * 0.004f,
                                static_cast<float>(z) * 0.004f, seed + 4400, 3,
                                0.5f, 2.0f));
  const float path =
      NormalizedFBM2D(static_cast<float>(x) * 0.015f,
                      static_cast<float>(z) * 0.015f, seed + 4401, 2, 0.5f,
                      2.0f);
  const float combined = ridge * path;
  if (combined < 0.82f)
  {
    return;
  }

  const float depthFactor = Smoothstep(0.82f, 0.95f, combined);
  const int carveDepth =
      params.minDepth +
      static_cast<int>(depthFactor *
                       static_cast<float>(params.maxDepth - params.minDepth));

  const float centerNoise =
      NormalizedFBM2D(static_cast<float>(x) * 0.02f,
                      static_cast<float>(z) * 0.02f, seed + 4402, 2, 0.5f,
                      2.0f);
  const int centerX = x + static_cast<int>((centerNoise - 0.5f) * 6.0f);
  const int centerZ = z + static_cast<int>((path - 0.5f) * 6.0f);

  for (int dx = -3; dx <= 3; ++dx)
  {
    for (int dz = -3; dz <= 3; ++dz)
    {
      const int wx = x + dx;
      const int wz = z + dz;
      const float dist = std::sqrt(static_cast<float>(dx * dx + dz * dz));
      const float widthProfile = 1.0f - std::clamp(dist / 3.5f, 0.0f, 1.0f);
      const int columnDepth =
          static_cast<int>(static_cast<float>(carveDepth) * widthProfile);
      if (columnDepth <= 0)
      {
        continue;
      }
      for (int dy = 0; dy < columnDepth; ++dy)
      {
        const int y = surfaceY - dy;
        if (y < 1)
        {
          break;
        }
        const glm::ivec3 pos(wx, y, wz);
        if (!ctx.World.IsAir(pos))
        {
          ctx.World.SetBlock(pos, BLOCK_AIR);
        }
      }
    }
  }
  ctx.AccumulateDirtyColumn(std::max(1, surfaceY - carveDepth), surfaceY);
  (void)centerX;
  (void)centerZ;
}

} // namespace cutum
