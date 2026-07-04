#include "World/Core/FluidColumnSurfaceQuery.h"

#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"
#include "World/Math/GridMath.h"

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

} // namespace cutum
