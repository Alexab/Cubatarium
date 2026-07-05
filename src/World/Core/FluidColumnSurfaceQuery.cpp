#include "World/Core/FluidColumnSurfaceQuery.h"

#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"
#include "World/Math/GridMath.h"

#include <algorithm>

namespace cutum
{

FluidColumnSurface FindFluidColumnSurfaceAt(const UBlockWorld &world,
                                            const UBlockRegistry &registry,
                                            int bx, int bz, int hintY,
                                            int scanUp, int scanDown)
{
  FluidColumnSurface column;
  for (int y = hintY + scanUp; y >= hintY - scanDown; --y)
  {
    const BlockId id = world.GetBlock(glm::ivec3(bx, y, bz));
    if (!registry.IsLiquid(id))
    {
      continue;
    }
    if (registry.GetRenderStyle(id) != BlockRenderStyle::Fluid)
    {
      continue;
    }
    column.fluidId = id;
    column.surfaceBlockY = y;
    column.surfaceY = BlockTopY(y);
    column.valid = true;
    return column;
  }
  return column;
}

bool HasFluidSurfaceNear(const UBlockWorld &world, const UBlockRegistry &registry,
                         int bx, int bz, int hintY, int radiusBlocks)
{
  constexpr int kStride = 16;
  constexpr int kScanUp = 24;
  constexpr int kScanDown = 24;
  const int radius = std::max(0, radiusBlocks);
  for (int dx = -radius; dx <= radius; dx += kStride)
  {
    for (int dz = -radius; dz <= radius; dz += kStride)
    {
      if (FindFluidColumnSurfaceAt(world, registry, bx + dx, bz + dz, hintY,
                                   kScanUp, kScanDown)
              .valid)
      {
        return true;
      }
    }
  }
  return false;
}

} // namespace cutum
