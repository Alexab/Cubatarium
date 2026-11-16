#include "World/Core/FluidColumnSurfaceQuery.h"

#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"
#include "World/Math/GridMath.h"

#include <cstdint>

namespace cutum
{

bool ContainsFluidAtHeight(const FluidColumnSurface &column, float eyeY)
{
  return column.valid &&
         eyeY >= static_cast<float>(column.bottomBlockY) - 0.5f &&
         eyeY < column.surfaceY;
}

FluidColumnSurface FindFluidColumnSurfaceForEye(const UBlockWorld &world,
                                                const UBlockRegistry &registry,
                                                const glm::vec3 &eye)
{
  FluidColumnSurface column;
  const glm::ivec3 block = WorldPosToBlock(eye);
  const auto &profile = world.GetVerticalProfile();
  if (!profile.IsValid() || !profile.ContainsY(block.y))
    return column;
  const auto liquid = [&](int y)
  {
    const BlockId id = world.GetBlock(glm::ivec3(block.x, y, block.z));
    return id != BLOCK_AIR && registry.IsLiquid(id) &&
                   registry.GetRenderStyle(id) == BlockRenderStyle::Fluid
               ? id
               : BLOCK_AIR;
  };
  int anchor = block.y;
  BlockId id = liquid(anchor);
  // Keep the existing quarter-block surface transition available when the eye
  // has just emerged. Never search through a solid cell or a remote air gap.
  if (id == BLOCK_AIR && anchor > profile.MinY &&
      world.GetBlock(block) == BLOCK_AIR &&
      eye.y - BlockTopY(anchor - 1) < 0.25f)
  {
    --anchor;
    id = liquid(anchor);
  }
  if (id == BLOCK_AIR)
    return column;
  int top = anchor;
  int bottom = anchor;
  while (top < profile.MaxY && liquid(top + 1) == id)
    ++top;
  while (bottom > profile.MinY && liquid(bottom - 1) == id)
    --bottom;
  column.fluidId = id;
  column.surfaceBlockY = top;
  column.bottomBlockY = bottom;
  column.surfaceY = BlockTopY(top);
  column.valid = true;
  return column;
}

FluidColumnSurface FindFluidColumnSurfaceAt(const UBlockWorld &world,
                                            const UBlockRegistry &registry,
                                            int bx, int bz, int hintY,
                                            int scanUp, int scanDown)
{
  FluidColumnSurface column;
  int max_y = INT32_MIN;
  BlockId top_fluid_id = BLOCK_AIR;
  for (int y = hintY + scanUp; y >= hintY - scanDown; --y)
  {
    const BlockId id = world.GetBlock(glm::ivec3(bx, y, bz));
    if (id == BLOCK_AIR || !registry.IsLiquid(id))
    {
      continue; // air is never liquid: skip the registry lookup
    }
    if (registry.GetRenderStyle(id) != BlockRenderStyle::Fluid)
    {
      continue;
    }
    if (y > max_y)
    {
      max_y = y;
      top_fluid_id = id;
    }
  }
  if (max_y == INT32_MIN)
  {
    return column;
  }

  int bottom_y = max_y;
  for (int y = max_y - 1; y >= hintY - scanDown; --y)
  {
    const BlockId id = world.GetBlock(glm::ivec3(bx, y, bz));
    if (!registry.IsLiquid(id) ||
        registry.GetRenderStyle(id) != BlockRenderStyle::Fluid)
    {
      break;
    }
    bottom_y = y;
  }

  column.fluidId = top_fluid_id;
  column.surfaceBlockY = max_y;
  column.bottomBlockY = bottom_y;
  column.surfaceY = BlockTopY(max_y);
  column.valid = true;
  return column;
}

bool HasFluidSurfaceNear(const UBlockWorld &world,
                         const UBlockRegistry &registry, int bx, int bz,
                         int hintY, int radiusBlocks)
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
