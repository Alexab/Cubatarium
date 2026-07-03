#include "World/Objects/ObjectUtil.h"
#include "Blocks/BlockDefinition.h"
#include "Blocks/BlockRegistry.h"
#include <algorithm>
#include <array>

namespace cutum
{

namespace
{

bool HasBlockSupportBelow(const UBlockWorld &world, UBlockRegistry &registry,
                          glm::ivec3 pos)
{
  const glm::ivec3 below(pos.x, pos.y - 1, pos.z);
  const BlockId below_id = world.GetBlock(below);
  if (below_id != BLOCK_AIR && registry.BlocksMovement(below_id))
  {
    return true;
  }
  static constexpr std::array<glm::ivec3, 4> kBelowOffsets = {
      glm::ivec3(1, -1, 0), glm::ivec3(-1, -1, 0), glm::ivec3(0, -1, 1),
      glm::ivec3(0, -1, -1)};
  for (const glm::ivec3 &offset : kBelowOffsets)
  {
    const BlockId id = world.GetBlock(pos + offset);
    if (id != BLOCK_AIR && registry.BlocksMovement(id))
    {
      return true;
    }
  }
  return false;
}

} // namespace

bool CanPlaceObjectAt(const UBlockWorld &world,
                      const WorldObjectDefinition &object,
                      glm::ivec3 anchorWorldPos)
{
  for (const auto &voxel : object.voxels)
  {
    const glm::ivec3 worldPos = anchorWorldPos + voxel.offset - object.anchor;
    if (!world.IsAir(worldPos))
    {
      return false;
    }
  }
  return !object.voxels.empty();
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

bool CanPlaceObjectAtForWorldGen(const UBlockWorld &world,
                                 UBlockRegistry &registry,
                                 const WorldObjectDefinition &object,
                                 glm::ivec3 anchorWorldPos, int maxScanY,
                                 int seaLevel, int anchorSurfaceY)
{
  for (const auto &voxel : object.voxels)
  {
    const glm::ivec3 worldPos = anchorWorldPos + voxel.offset - object.anchor;
    const int localSurface =
        FindTopSolidSurfaceY(world, registry, worldPos.x, worldPos.z, maxScanY);
    const bool voxelSolid =
        voxel.Id != BLOCK_AIR && registry.BlocksMovement(voxel.Id);
    if (localSurface < 0)
    {
      if (!world.IsAir(worldPos))
      {
        return false;
      }
      if (seaLevel >= 0 && anchorSurfaceY >= 0 && anchorSurfaceY < seaLevel &&
          voxelSolid)
      {
        return false;
      }
      if (seaLevel >= 0 && voxelSolid && worldPos.y <= seaLevel)
      {
        return false;
      }
      continue;
    }
    if (seaLevel >= 0 && voxelSolid && localSurface < seaLevel &&
        worldPos.y <= seaLevel)
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
  return !object.voxels.empty();
}

ObjectPlacementStats PlaceObjectAt(UBlockWorld &world,
                                   const WorldObjectDefinition &object,
                                   glm::ivec3 anchorWorldPos, bool skipOccupied)
{
  ObjectPlacementStats stats;
  if (object.voxels.empty())
  {
    return stats;
  }

  stats.minY = anchorWorldPos.y;
  stats.maxY = anchorWorldPos.y;

  for (const auto &voxel : object.voxels)
  {
    const glm::ivec3 worldPos = anchorWorldPos + voxel.offset - object.anchor;
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

std::vector<glm::ivec3> BreakUnsupportedBlocksAbove(UBlockWorld &world,
                                                      UBlockRegistry &registry,
                                                      glm::ivec3 groundPos,
                                                      int maxY)
{
  std::vector<glm::ivec3> broken;
  glm::ivec3 pos(groundPos.x, groundPos.y + 1, groundPos.z);
  while (pos.y <= maxY)
  {
    const BlockId id = world.GetBlock(pos);
    if (id == BLOCK_AIR)
    {
      break;
    }
    if (registry.IsLiquid(id))
    {
      break;
    }
    if (registry.BlocksMovement(id) &&
        HasBlockSupportBelow(world, registry, pos))
    {
      break;
    }
    world.SetBlock(pos, BLOCK_AIR);
    world.ClearFluidState(pos);
    broken.push_back(pos);
    ++pos.y;
  }
  return broken;
}

} // namespace cutum
