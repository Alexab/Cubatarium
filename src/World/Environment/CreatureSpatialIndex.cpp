#include "World/Environment/CreatureSpatialIndex.h"
#include "World/Math/CollisionVolume.h"
#include "World/Math/GridMath.h"
#include <algorithm>
#include <cmath>

namespace cutum
{

void UCreatureSpatialIndex::Clear()
{
  Cells.clear();
  Tracked.clear();
}

UCreatureSpatialIndex::CellKey
UCreatureSpatialIndex::CellForPosition(const glm::vec3 &origin)
{
  CellKey key;
  key.x = WorldCoordToBlockIndex(origin.x) >> 1;
  key.z = WorldCoordToBlockIndex(origin.z) >> 1;
  return key;
}

void UCreatureSpatialIndex::GatherCellsForVolume(const CollisionVolume &vol,
                                                  std::vector<CellKey> &out_cells)
{
  // Every cell (2x2 blocks) covering the body's block range plus one block
  // of margin, per axis. Inserts and queries use the same rule, so two
  // overlapping AABBs always share a cell. This used to sample
  // (2r+1)^2 blocks with r from the larger half extent on both axes and
  // dedupe each sample linearly (a whale: 225 samples x ~64 compares).
  out_cells.clear();
  const glm::ivec3 center = WorldPosToBlock(vol.center);
  const int radiusX = static_cast<int>(std::ceil(vol.halfExtents.x)) + 1;
  const int radiusZ = static_cast<int>(std::ceil(vol.halfExtents.z)) + 1;
  // Same mapping as CellForPosition (arithmetic shift floors negatives).
  const int cellMinX = (center.x - radiusX) >> 1;
  const int cellMaxX = (center.x + radiusX) >> 1;
  const int cellMinZ = (center.z - radiusZ) >> 1;
  const int cellMaxZ = (center.z + radiusZ) >> 1;
  out_cells.reserve(static_cast<size_t>((cellMaxX - cellMinX + 1) *
                                        (cellMaxZ - cellMinZ + 1)));
  for (int x = cellMinX; x <= cellMaxX; ++x)
  {
    for (int z = cellMinZ; z <= cellMaxZ; ++z)
    {
      CellKey key;
      key.x = x;
      key.z = z;
      out_cells.push_back(key);
    }
  }
}

void UCreatureSpatialIndex::RemoveEntryFromCells(const Entry &entry)
{
  CollisionVolume vol;
  vol.center = entry.volCenter;
  vol.halfExtents = entry.halfExtents;
  std::vector<CellKey> keys;
  GatherCellsForVolume(vol, keys);
  for (const CellKey &key : keys)
  {
    const auto it = Cells.find(key);
    if (it == Cells.end())
    {
      continue;
    }
    auto &bucket = it->second;
    bucket.erase(std::remove_if(bucket.begin(), bucket.end(),
                                [&](const Entry &candidate)
                                { return candidate.Id == entry.Id; }),
                 bucket.end());
    if (bucket.empty())
    {
      Cells.erase(it);
    }
  }
}

void UCreatureSpatialIndex::InsertEntryIntoCells(const Entry &entry)
{
  // Insert into every cell the entry's AABB touches, mirroring
  // RemoveEntryFromCells: queries gather cells by query volume, so a
  // single-cell insert lets wide bodies slip past overlap checks.
  CollisionVolume vol;
  vol.center = entry.volCenter;
  vol.halfExtents = entry.halfExtents;
  std::vector<CellKey> keys;
  GatherCellsForVolume(vol, keys);
  for (const CellKey &key : keys)
  {
    Cells[key].push_back(entry);
  }
}

void UCreatureSpatialIndex::Upsert(CreatureId id, const glm::vec3 &origin,
                                   const glm::vec3 &half_extents)
{
  if (id == 0)
  {
    return;
  }
  const auto tracked_it = Tracked.find(id);
  if (tracked_it != Tracked.end())
  {
    const Entry &existing = tracked_it->second;
    if (glm::length(existing.bodyOrigin - origin) < 1e-4f &&
        glm::length(existing.halfExtents - half_extents) < 1e-4f)
    {
      return;
    }
    RemoveEntryFromCells(existing);
  }
  Entry entry;
  entry.Id = id;
  entry.bodyOrigin = origin;
  entry.halfExtents = half_extents;
  // bodyOrigin is the FEET position; the collision AABB is centered at
  // feet + half height (matches UCreature::GetCollisionVolume).
  entry.volCenter = origin + glm::vec3(0.0f, half_extents.y, 0.0f);
  Tracked[id] = entry;
  InsertEntryIntoCells(entry);
}

void UCreatureSpatialIndex::Remove(CreatureId id)
{
  const auto tracked_it = Tracked.find(id);
  if (tracked_it == Tracked.end())
  {
    return;
  }
  RemoveEntryFromCells(tracked_it->second);
  Tracked.erase(tracked_it);
}

void UCreatureSpatialIndex::PruneExcept(const std::unordered_set<CreatureId> &alive)
{
  std::vector<CreatureId> stale;
  stale.reserve(Tracked.size());
  for (const auto &entry : Tracked)
  {
    if (alive.find(entry.first) == alive.end())
    {
      stale.push_back(entry.first);
    }
  }
  for (const CreatureId id : stale)
  {
    Remove(id);
  }
}

void UCreatureSpatialIndex::Rebuild(
    const std::function<void(const std::function<void(
        CreatureId, const glm::vec3 &, const glm::vec3 &)> &)> &enumerator)
{
  Clear();
  enumerator(
      [this](CreatureId id, const glm::vec3 &origin,
             const glm::vec3 &half_extents) { Upsert(id, origin, half_extents); });
}

std::vector<CreatureId>
UCreatureSpatialIndex::QueryRadius(const glm::vec3 &center, float radius,
                                   CreatureId skip_id) const
{
  std::vector<CreatureId> out;
  // Entries occupy every cell their AABB touches, so a query walking
  // neighboring cells can meet the same entry twice — report each once.
  std::vector<CreatureId> seen;
  const float radius_sq = radius * radius;
  const int cell_radius = static_cast<int>(std::ceil(radius / 2.0f)) + 1;
  const CellKey center_cell = CellForPosition(center);
  for (int dx = -cell_radius; dx <= cell_radius; ++dx)
  {
    for (int dz = -cell_radius; dz <= cell_radius; ++dz)
    {
      const CellKey key{center_cell.x + dx, center_cell.z + dz};
      const auto it = Cells.find(key);
      if (it == Cells.end())
      {
        continue;
      }
      for (const Entry &entry : it->second)
      {
        if (entry.Id == skip_id ||
            std::find(seen.begin(), seen.end(), entry.Id) != seen.end())
        {
          continue;
        }
        const glm::vec3 delta = entry.bodyOrigin - center;
        const float dist_sq = delta.x * delta.x + delta.z * delta.z;
        if (dist_sq <= radius_sq)
        {
          seen.push_back(entry.Id);
          out.push_back(entry.Id);
        }
      }
    }
  }
  return out;
}

std::vector<CreatureNeighborView>
UCreatureSpatialIndex::QueryNeighbors(const glm::vec3 &center, float radius,
                                      CreatureId skip_id) const
{
  std::vector<CreatureNeighborView> out;
  std::vector<CreatureId> seen;
  const float radius_sq = radius * radius;
  const int cell_radius = static_cast<int>(std::ceil(radius / 2.0f)) + 1;
  const CellKey center_cell = CellForPosition(center);
  for (int dx = -cell_radius; dx <= cell_radius; ++dx)
  {
    for (int dz = -cell_radius; dz <= cell_radius; ++dz)
    {
      const CellKey key{center_cell.x + dx, center_cell.z + dz};
      const auto it = Cells.find(key);
      if (it == Cells.end())
      {
        continue;
      }
      for (const Entry &entry : it->second)
      {
        if (entry.Id == skip_id ||
            std::find(seen.begin(), seen.end(), entry.Id) != seen.end())
        {
          continue;
        }
        const glm::vec3 delta = entry.bodyOrigin - center;
        const float dist_sq = delta.x * delta.x + delta.z * delta.z;
        if (dist_sq <= radius_sq)
        {
          CreatureNeighborView neighbor;
          neighbor.Id = entry.Id;
          neighbor.bodyOrigin = entry.bodyOrigin;
          seen.push_back(entry.Id);
          out.push_back(neighbor);
        }
      }
    }
  }
  return out;
}

std::vector<CreatureNeighborView>
UCreatureSpatialIndex::QueryNeighborsOverlappingVolume(
    const CollisionVolume &vol, CreatureId skip_id) const
{
  std::vector<CreatureNeighborView> out;
  std::vector<CreatureId> seen;
  std::vector<CellKey> keys;
  GatherCellsForVolume(vol, keys);
  for (const CellKey &key : keys)
  {
    const auto it = Cells.find(key);
    if (it == Cells.end())
    {
      continue;
    }
    for (const Entry &entry : it->second)
    {
      if (entry.Id == skip_id ||
          std::find(seen.begin(), seen.end(), entry.Id) != seen.end())
      {
        continue;
      }
      if (AabbOverlap(vol.center, vol.halfExtents, entry.volCenter,
                      entry.halfExtents))
      {
        CreatureNeighborView neighbor;
        neighbor.Id = entry.Id;
        neighbor.bodyOrigin = entry.bodyOrigin;
        seen.push_back(entry.Id);
        out.push_back(neighbor);
      }
    }
  }
  return out;
}

bool UCreatureSpatialIndex::AnyCreatureVolumeOverlaps(const CollisionVolume &vol,
                                                    CreatureId skip_id) const
{
  std::vector<CellKey> keys;
  GatherCellsForVolume(vol, keys);
  for (const CellKey &key : keys)
  {
    const auto it = Cells.find(key);
    if (it == Cells.end())
    {
      continue;
    }
    for (const Entry &entry : it->second)
    {
      if (entry.Id == skip_id)
      {
        continue;
      }
      if (AabbOverlap(vol.center, vol.halfExtents, entry.volCenter,
                      entry.halfExtents))
      {
        return true;
      }
    }
  }
  return false;
}

} // namespace cutum
