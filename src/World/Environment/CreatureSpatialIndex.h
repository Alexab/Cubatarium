#ifndef CREATURESPATIALINDEX_H
#define CREATURESPATIALINDEX_H

#include "Activity/CreatureActivityTypes.h"
#include "World/Math/CollisionVolume.h"
#include <functional>
#include <glm/glm.hpp>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cutum
{

class UCreatureSpatialIndex
{
public:
  void Clear();
  void Rebuild(
      const std::function<void(const std::function<void(
          CreatureId, const glm::vec3 &, const glm::vec3 &)> &)> &enumerator);
  void Upsert(CreatureId id, const glm::vec3 &origin,
              const glm::vec3 &half_extents);
  void Remove(CreatureId id);
  void PruneExcept(const std::unordered_set<CreatureId> &alive);
  std::vector<CreatureId> QueryRadius(const glm::vec3 &center, float radius,
                                      CreatureId skip_id) const;
  std::vector<CreatureNeighborView>
  QueryNeighbors(const glm::vec3 &center, float radius,
                 CreatureId skip_id) const;
  bool AnyCreatureVolumeOverlaps(const struct CollisionVolume &vol,
                                 CreatureId skip_id) const;

private:
  struct CellKey
  {
    int x{0};
    int z{0};
    bool operator==(const CellKey &other) const
    {
      return x == other.x && z == other.z;
    }
  };

  struct CellKeyHash
  {
    size_t operator()(const CellKey &key) const
    {
      return std::hash<int>()(key.x) ^ (std::hash<int>()(key.z) << 1);
    }
  };

  struct Entry
  {
    CreatureId Id{0};
    glm::vec3 bodyOrigin{0.0f};
    glm::vec3 halfExtents{0.3f, 0.9f, 0.3f};
  };

  static CellKey CellForPosition(const glm::vec3 &origin);
  static void GatherCellsForVolume(const struct CollisionVolume &vol,
                                   std::vector<CellKey> &out_cells);
  void RemoveEntryFromCells(const Entry &entry);
  void InsertEntryIntoCells(const Entry &entry);

  std::unordered_map<CellKey, std::vector<Entry>, CellKeyHash> Cells;
  std::unordered_map<CreatureId, Entry> Tracked;
};

} // namespace cutum

#endif
