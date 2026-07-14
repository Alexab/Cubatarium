#include "World/Objects/ObjectUtil.h"
#include "Blocks/BlockDefinition.h"
#include "Blocks/BlockRegistry.h"
#include "World/Math/BlockTypes.h"
#include "WorldGen/Core/WorldGenPlacementTuning.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace cutum
{

namespace
{

constexpr int kLeafNearSolidRadius = WorldGenPlacementTuning::LeafNearSolidRadius;

using ColumnXZ = std::pair<int, int>;

struct ColumnXZHash
{
  size_t operator()(const ColumnXZ &column) const
  {
    return std::hash<int64_t>{}(
        (static_cast<int64_t>(static_cast<uint32_t>(column.first)) << 32) ^
        static_cast<uint64_t>(static_cast<uint32_t>(column.second)));
  }
};

ColumnXZ ColumnKey(int x, int z)
{
  return {x, z};
}

int ChebyshevDistance3(const glm::ivec3 &a, const glm::ivec3 &b)
{
  return std::max({std::abs(a.x - b.x), std::abs(a.y - b.y), std::abs(a.z - b.z)});
}

} // namespace

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

BlockId ResolveObjectVoxelPlacementId(const ObjectVoxel &voxel,
                                      const UBlockRegistry &registry)
{
  if (!voxel.Type.empty())
  {
    const BlockId resolved = registry.GetPackBlockIdByTypeName(voxel.Type);
    if (resolved != BLOCK_AIR)
    {
      return resolved;
    }
  }
  if (voxel.Id != BLOCK_AIR && voxel.Id < kRuntimeBlockIdMin)
  {
    return voxel.Id;
  }
  return voxel.Id;
}

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

bool CanOccupySurfacePlantLayerAt(const UBlockWorld &world,
                                  UBlockRegistry &registry, glm::ivec3 worldPos)
{
  const glm::ivec3 ground = worldPos - glm::ivec3(0, 1, 0);
  if (!IsSolidPlantGround(world, registry, ground))
  {
    return false;
  }
  const BlockId id = world.GetBlock(worldPos);
  return id == BLOCK_AIR || !registry.BlocksMovement(id);
}

bool HasOpenSurfaceAbove(const UBlockWorld &world, UBlockRegistry &registry,
                         int x, int y, int z, int maxY)
{
  const int limit =
      std::min(maxY, y + WorldGenPlacementTuning::SurfacePlantClearance);
  for (int scanY = y + 1; scanY <= limit; ++scanY)
  {
    const BlockId id = world.GetBlock(glm::ivec3(x, scanY, z));
    if (id == BLOCK_AIR)
    {
      return true;
    }
    if (!registry.BlocksMovement(id))
    {
      continue;
    }
    return false;
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
  return HasOpenSurfaceAbove(world, registry, x, surfaceY, z,
                             surfaceY +
                                 WorldGenPlacementTuning::SurfacePlantClearance);
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
    if (HasOpenSurfaceAbove(world, registry, x, y, z, maxY))
    {
      return y;
    }
  }
  return -1;
}

namespace
{

bool ObjectVoxelIsSolid(const ObjectVoxel &voxel,
                        const UBlockRegistry &registry)
{
  const BlockId id = ResolveObjectVoxelPlacementId(voxel, registry);
  return id != BLOCK_AIR && registry.BlocksMovement(id);
}

std::optional<int> UniformSurfaceLayerSolidDy(const WorldObjectDefinition &object,
                                              const UBlockRegistry &registry)
{
  if (object.PlacementMode != ObjectPlacementMode::SurfaceLayer)
  {
    return std::nullopt;
  }
  std::optional<int> solidDy;
  for (const auto &voxel : object.voxels)
  {
    if (!ObjectVoxelIsSolid(voxel, registry))
    {
      continue;
    }
    if (!solidDy.has_value())
    {
      solidDy = voxel.offset.y;
      continue;
    }
    if (*solidDy != voxel.offset.y)
    {
      return std::nullopt;
    }
  }
  return solidDy;
}

glm::ivec3 ResolveObjectVoxelWorldPos(const UBlockWorld & /*world*/,
                                      glm::ivec3 anchorWorldPos,
                                      const WorldObjectDefinition &object,
                                      const ObjectVoxel &voxel,
                                      UBlockRegistry & /*registry*/,
                                      int /*maxScanY*/)
{
  return anchorWorldPos + voxel.offset - object.anchor;
}

} // namespace

PlacementSurfaceInfo ResolvePlacementSurfaceY(const UBlockWorld &world,
                                              UBlockRegistry &registry, int x,
                                              int z, int heightmapSurfaceY,
                                              int maxHeight, int seaLevel)
{
  PlacementSurfaceInfo info;
  info.maxScanY = ComputeMaxScanY(heightmapSurfaceY, seaLevel, maxHeight);
  info.topSolidY =
      FindTopSolidSurfaceY(world, registry, x, z, info.maxScanY);
  if (info.topSolidY < seaLevel + WorldGenPlacementTuning::MinLandAboveSea)
  {
    info.topSolidY = -1;
  }
  return info;
}

bool IsRemovableFloatingPlantBlock(const UBlockRegistry &registry, BlockId id)
{
  if (id == BLOCK_AIR || registry.IsLiquid(id))
  {
    return false;
  }
  if (registry.IsFluidPermeable(id))
  {
    return true;
  }
  return !registry.BlocksMovement(id);
}

bool ColumnSolidYsContiguous(const std::vector<int> &ys)
{
  if (ys.empty())
  {
    return false;
  }
  for (size_t i = 1; i < ys.size(); ++i)
  {
    if (ys[i] != ys[i - 1] + 1)
    {
      return false;
    }
  }
  return true;
}

bool CanOccupySolidVoxelForWorldGen(const UBlockWorld &world,
                                    UBlockRegistry &registry, glm::ivec3 pos,
                                    int columnLocalSurface)
{
  if (world.IsAir(pos))
  {
    return true;
  }
  const BlockId existing = world.GetBlock(pos);
  if (!registry.BlocksMovement(existing))
  {
    return true;
  }
  if (columnLocalSurface >= 0 && pos.y == columnLocalSurface &&
      IsSolidPlantGround(world, registry, pos))
  {
    return true;
  }
  return false;
}

bool IsSurfaceLayerPrefab(const WorldObjectDefinition &object,
                          UBlockRegistry &registry)
{
  if (object.PlacementMode == ObjectPlacementMode::SurfaceLayer)
  {
    return true;
  }
  if (object.PlacementMode == ObjectPlacementMode::VerticalPlant)
  {
    return false;
  }
  std::optional<int> solidDy;
  for (const auto &voxel : object.voxels)
  {
    if (!ObjectVoxelIsSolid(voxel, registry))
    {
      continue;
    }
    if (!solidDy.has_value())
    {
      solidDy = voxel.offset.y;
      continue;
    }
    if (*solidDy != voxel.offset.y)
    {
      return false;
    }
  }
  return solidDy.has_value();
}

int ResolveWorldGenAnchorY(const WorldObjectDefinition &object,
                           UBlockRegistry &registry, int topSolid,
                           int yOffset)
{
  const int effectiveYOffset = std::max(yOffset, 0);
  switch (object.PlacementMode)
  {
  case ObjectPlacementMode::SurfaceLayer:
    return topSolid + effectiveYOffset;
  case ObjectPlacementMode::VerticalPlant:
    return topSolid + 1 + effectiveYOffset;
  default:
    break;
  }
  static std::unordered_set<std::string> warnedPrefabs;
  if (!object.Name.empty() && warnedPrefabs.insert(object.Name).second)
  {
    std::cerr << "WorldGen: prefab '" << object.Name
              << "' missing placement.mode; using surface heuristic"
              << std::endl;
  }
  if (IsSurfaceLayerPrefab(object, registry))
  {
    return topSolid + effectiveYOffset;
  }
  return topSolid + 1 + effectiveYOffset;
}

bool ColumnSolidPlacementValid(const UBlockWorld &world,
                               UBlockRegistry &registry, int x, int z,
                               const std::vector<int> &ys, int maxScanY,
                               int seaLevel)
{
  const int localSurface =
      FindTopSolidSurfaceY(world, registry, x, z, maxScanY);
  if (localSurface < 0)
  {
    return false;
  }
  if (seaLevel >= 0 && localSurface < seaLevel)
  {
    return false;
  }
  if (!ColumnSolidYsContiguous(ys))
  {
    return false;
  }
  const int minY = ys.front();
  const int maxY = ys.back();
  if (minY < localSurface || minY > localSurface + 1)
  {
    return false;
  }
  if (minY == localSurface + 1)
  {
    if (!CanOccupySurfacePlantLayerAt(world, registry, glm::ivec3(x, minY, z)))
    {
      return false;
    }
  }
  else if (!IsSolidPlantGround(world, registry, glm::ivec3(x, localSurface, z)))
  {
    return false;
  }
  if (seaLevel >= 0 && maxY <= seaLevel)
  {
    return false;
  }
  return true;
}

bool CanPlaceObjectAtForWorldGen(const UBlockWorld &world,
                                 UBlockRegistry &registry,
                                 const WorldObjectDefinition &object,
                                 glm::ivec3 anchorWorldPos, int maxScanY,
                                 int seaLevel)
{
  if (object.voxels.empty())
  {
    return false;
  }

  const int anchorX = anchorWorldPos.x;
  const int anchorZ = anchorWorldPos.z;
  const int anchorLocalSurface =
      FindTopSolidSurfaceY(world, registry, anchorX, anchorZ, maxScanY);
  if (anchorLocalSurface < 0)
  {
    return false;
  }
  if (seaLevel >= 0 && anchorLocalSurface < seaLevel)
  {
    return false;
  }

  const bool surfaceLayer = IsSurfaceLayerPrefab(object, registry);

  if (object.PlacementMode == ObjectPlacementMode::SurfaceLayer)
  {
    if (!UniformSurfaceLayerSolidDy(object, registry).has_value())
    {
      return false;
    }
  }

  std::vector<glm::ivec3> solidWorldPositions;
  std::vector<glm::ivec3> nonSolidWorldPositions;
  solidWorldPositions.reserve(object.voxels.size());
  nonSolidWorldPositions.reserve(object.voxels.size());

  for (const auto &voxel : object.voxels)
  {
    const glm::ivec3 worldPos = ResolveObjectVoxelWorldPos(
        world, anchorWorldPos, object, voxel, registry, maxScanY);
    const bool voxelSolid = ObjectVoxelIsSolid(voxel, registry);
    if (voxelSolid)
    {
      solidWorldPositions.push_back(worldPos);
    }
    else if (ResolveObjectVoxelPlacementId(voxel, registry) != BLOCK_AIR)
    {
      nonSolidWorldPositions.push_back(worldPos);
    }
  }

  if (solidWorldPositions.empty() && !nonSolidWorldPositions.empty())
  {
    return false;
  }

  std::unordered_map<ColumnXZ, std::vector<int>, ColumnXZHash> solidYByColumn;
  for (const glm::ivec3 &pos : solidWorldPositions)
  {
    solidYByColumn[ColumnKey(pos.x, pos.z)].push_back(pos.y);
  }

  const ColumnXZ anchorKey = ColumnKey(anchorX, anchorZ);
  const auto anchorIt = solidYByColumn.find(anchorKey);
  if (solidYByColumn.empty() || anchorIt == solidYByColumn.end())
  {
    return false;
  }

  if (surfaceLayer)
  {
    const std::optional<int> uniformSolidDy =
        UniformSurfaceLayerSolidDy(object, registry);
    if (uniformSolidDy.has_value())
    {
      const int placeY = anchorWorldPos.y + (*uniformSolidDy - object.anchor.y);
      if (placeY < anchorLocalSurface || placeY > anchorLocalSurface + 1)
      {
        return false;
      }
    }
    for (const auto &entry : solidYByColumn)
    {
      const int colX = entry.first.first;
      const int colZ = entry.first.second;
      std::vector<int> ys = entry.second;
      std::sort(ys.begin(), ys.end());
      if (!ColumnSolidPlacementValid(world, registry, colX, colZ, ys, maxScanY,
                                     seaLevel))
      {
        return false;
      }
    }
  }
  else
  {
    {
      std::vector<int> anchorYs = anchorIt->second;
      std::sort(anchorYs.begin(), anchorYs.end());
      if (!ColumnSolidPlacementValid(world, registry, anchorX, anchorZ,
                                     anchorYs, maxScanY, seaLevel))
      {
        return false;
      }
    }

    for (const auto &entry : solidYByColumn)
    {
      if (entry.first == anchorKey)
      {
        continue;
      }
      std::vector<int> ys = entry.second;
      std::sort(ys.begin(), ys.end());
      if (!ColumnSolidYsContiguous(ys))
      {
        return false;
      }
      if (ys.front() <= anchorLocalSurface)
      {
        return false;
      }
    }
  }

  for (const glm::ivec3 &worldPos : solidWorldPositions)
  {
    const int columnLocalSurface =
        FindTopSolidSurfaceY(world, registry, worldPos.x, worldPos.z, maxScanY);
    if (!CanOccupySolidVoxelForWorldGen(world, registry, worldPos,
                                        columnLocalSurface))
    {
      return false;
    }
  }

  for (const glm::ivec3 &worldPos : nonSolidWorldPositions)
  {
    if (!world.IsAir(worldPos))
    {
      return false;
    }
    bool nearSolid = false;
    for (const glm::ivec3 &solidPos : solidWorldPositions)
    {
      if (ChebyshevDistance3(worldPos, solidPos) <= kLeafNearSolidRadius)
      {
        nearSolid = true;
        break;
      }
    }
    if (!nearSolid)
    {
      return false;
    }
  }

  return true;
}

ObjectPlacementStats PlaceObjectAt(UBlockWorld &world,
                                   UBlockRegistry &registry,
                                   const WorldObjectDefinition &object,
                                   glm::ivec3 anchorWorldPos, bool skipOccupied,
                                   const ObjectWorldGenPlacementOptions &options)
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
    const glm::ivec3 worldPos = ResolveObjectVoxelWorldPos(
        world, anchorWorldPos, object, voxel, registry, options.maxScanY);
    const BlockId blockId = ResolveObjectVoxelPlacementId(voxel, registry);
    if (blockId == BLOCK_AIR)
    {
      continue;
    }
    if (skipOccupied && !world.IsAir(worldPos))
    {
      continue;
    }
    world.SetBlock(worldPos, blockId);
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

int PruneFloatingPlantsInColumn(UBlockWorld &world, UBlockRegistry &registry,
                                int x, int z, int maxY)
{
  const int topSolid =
      FindTopSolidSurfaceY(world, registry, x, z, maxY);
  if (topSolid < 0)
  {
    return 0;
  }

  int removed = 0;
  bool changed = true;
  while (changed)
  {
    changed = false;
    for (int y = topSolid + 2; y <= maxY; ++y)
    {
      const glm::ivec3 pos(x, y, z);
      const BlockId id = world.GetBlock(pos);
      if (!IsRemovableFloatingPlantBlock(registry, id))
      {
        continue;
      }
      if (HasBlockSupportBelow(world, registry, pos))
      {
        continue;
      }
      world.SetBlock(pos, BLOCK_AIR);
      world.ClearFluidState(pos);
      ++removed;
      changed = true;
    }
  }
  return removed;
}

} // namespace cutum
