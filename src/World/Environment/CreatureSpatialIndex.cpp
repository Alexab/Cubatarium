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
  out_cells.clear();
  const int radius =
      static_cast<int>(std::ceil(std::max(vol.halfExtents.x, vol.halfExtents.z))) +
      1;
  const glm::ivec3 center = WorldPosToBlock(vol.center);
  for (int dx = -radius; dx <= radius; ++dx)
  {
    for (int dz = -radius; dz <= radius; ++dz)
    {
      const glm::vec3 sample(static_cast<float>(center.x + dx),
                             vol.center.y,
                             static_cast<float>(center.z + dz));
      const CellKey key = CellForPosition(sample);
      if (std::find_if(out_cells.begin(), out_cells.end(),
                       [&](const CellKey &existing)
                       { return existing == key; }) == out_cells.end())
      {
        out_cells.push_back(key);
      }
    }
  }
}

void UCreatureSpatialIndex::RemoveEntryFromCells(const Entry &entry)
{
  CollisionVolume vol;
  vol.center = entry.bodyOrigin;
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
  Cells[CellForPosition(entry.bodyOrigin)].push_back(entry);
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
        if (entry.Id == skip_id)
        {
          continue;
        }
        const glm::vec3 delta = entry.bodyOrigin - center;
        const float dist_sq = delta.x * delta.x + delta.z * delta.z;
        if (dist_sq <= radius_sq)
        {
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
        if (entry.Id == skip_id)
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
          out.push_back(neighbor);
        }
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
      if (AabbOverlap(vol.center, vol.halfExtents, entry.bodyOrigin,
                      entry.halfExtents))
      {
        return true;
      }
    }
  }
  return false;
}

} // namespace cutum
