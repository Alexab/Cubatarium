#include "World/Prefabs/PrefabUtil.h"
#include "Blocks/BlockDefinition.h"
#include "Blocks/BlockRegistry.h"
#include <algorithm>

namespace cutum
{

bool CanPlacePrefabAt(const UBlockWorld &world, const Prefab &prefab,
                      glm::ivec3 anchorWorldPos)
{
  for (const auto &voxel : prefab.voxels)
  {
    const glm::ivec3 worldPos = anchorWorldPos + voxel.offset - prefab.anchor;
    if (!world.IsAir(worldPos))
    {
      return false;
    }
  }
  return !prefab.voxels.empty();
}

bool IsSolidPlantGround(const UBlockWorld &world, UBlockRegistry &registry,
                        glm::ivec3 groundPos)
{
  const BlockId id = world.GetBlock(groundPos);
  if (id == BLOCK_AIR || !registry.BlocksMovement(id))
  {
    return false;
  }
  return registry.GetRenderStyle(id) != BlockRenderStyle::Fluid;
}

bool CanPlacePlantAt(const UBlockWorld &world, UBlockRegistry &registry,
                     glm::ivec3 worldPos)
{
  if (!world.IsAir(worldPos))
  {
    return false;
  }
  const glm::ivec3 ground = worldPos - glm::ivec3(0, 1, 0);
  if (!IsSolidPlantGround(world, registry, ground))
  {
    return false;
  }
  for (int y = ground.y + 1; y < worldPos.y; ++y)
  {
    const BlockId between = world.GetBlock(glm::ivec3(worldPos.x, y, worldPos.z));
    if (between != BLOCK_AIR && !registry.BlocksMovement(between))
    {
      return false;
    }
  }
  return true;
}

bool IsExposedLandSurface(const UBlockWorld &world, UBlockRegistry &registry,
                          int x, int z, int surfaceY)
{
  if (!IsSolidPlantGround(world, registry, glm::ivec3(x, surfaceY, z)))
  {
    return false;
  }
  return world.IsAir(glm::ivec3(x, surfaceY + 1, z));
}

int FindTopSolidSurfaceY(const UBlockWorld &world, UBlockRegistry &registry,
                         int x, int z, int maxY)
{
  for (int y = std::max(1, maxY); y >= 1; --y)
  {
    const glm::ivec3 ground(x, y, z);
    if (!IsSolidPlantGround(world, registry, ground))
    {
      continue;
    }
    if (world.IsAir(glm::ivec3(x, y + 1, z)))
    {
      return y;
    }
  }
  return -1;
}

bool CanPlacePrefabAtForWorldGen(const UBlockWorld &world,
                                 UBlockRegistry &registry, const Prefab &prefab,
                                 glm::ivec3 anchorWorldPos, int maxScanY)
{
  for (const auto &voxel : prefab.voxels)
  {
    const glm::ivec3 worldPos = anchorWorldPos + voxel.offset - prefab.anchor;
    const int localSurface =
        FindTopSolidSurfaceY(world, registry, worldPos.x, worldPos.z, maxScanY);
    if (localSurface < 0)
    {
      return false;
    }
    if (worldPos.y > localSurface + 1)
    {
      if (!world.IsAir(worldPos))
      {
        return false;
      }
      for (int y = localSurface + 1; y < worldPos.y; ++y)
      {
        const BlockId between =
            world.GetBlock(glm::ivec3(worldPos.x, y, worldPos.z));
        if (between != BLOCK_AIR && !registry.BlocksMovement(between))
        {
          return false;
        }
      }
      continue;
    }
    if (worldPos.y == localSurface + 1)
    {
      if (!CanPlacePlantAt(world, registry, worldPos))
      {
        return false;
      }
      continue;
    }
    if (!world.IsAir(glm::ivec3(worldPos.x, localSurface + 1, worldPos.z)))
    {
      return false;
    }
    const BlockId existing = world.GetBlock(worldPos);
    if (existing == BLOCK_AIR)
    {
      return false;
    }
    if (!registry.BlocksMovement(existing) ||
        registry.GetRenderStyle(existing) == BlockRenderStyle::Fluid)
    {
      return false;
    }
  }
  return !prefab.voxels.empty();
}

PrefabPlacementStats PlacePrefabAt(UBlockWorld &world, const Prefab &prefab,
                                   glm::ivec3 anchorWorldPos, bool skipOccupied)
{
  PrefabPlacementStats stats;
  if (prefab.voxels.empty())
  {
    return stats;
  }

  stats.minY = anchorWorldPos.y;
  stats.maxY = anchorWorldPos.y;

  for (const auto &voxel : prefab.voxels)
  {
    const glm::ivec3 worldPos = anchorWorldPos + voxel.offset - prefab.anchor;
    if (skipOccupied && !world.IsAir(worldPos))
    {
      continue;
    }
    world.SetBlock(worldPos, voxel.Id);
    stats.minY = std::min(stats.minY, worldPos.y);
    stats.maxY = std::max(stats.maxY, worldPos.y);
    ++stats.placedCount;
  }
  return stats;
}

} // namespace cutum
