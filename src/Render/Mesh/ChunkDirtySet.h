#ifndef CHUNKDIRTYSET_H
#define CHUNKDIRTYSET_H

#include "World/Chunks/ChunkManager.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cutum
{

/// Deduped dual queue of chunk coords pending mesh rebuild.
/// FirstMeshQ ← MarkDirtyPriority; RemeshQ ← MarkDirty (Cruise wall P1).
/// Unified Queue is rebuilt lazily for legacy begin()/end() iteration.
class UChunkDirtySet
{
public:
  void MarkDirty(glm::ivec3 coord);
  void MarkDirtyPriority(glm::ivec3 coord);
  void Erase(glm::ivec3 coord);
  void Clear();
  bool IsFirstMesh(glm::ivec3 coord) const
  {
    return FirstMeshSet.find(coord) != FirstMeshSet.end();
  }

  size_t GetCount() const { return FirstMeshQ.size() + RemeshQ.size(); }
  size_t GetFirstMeshCount() const { return FirstMeshQ.size(); }
  size_t GetRemeshCount() const { return RemeshQ.size(); }
  bool empty() const { return FirstMeshQ.empty() && RemeshQ.empty(); }
  bool Contains(glm::ivec3 coord) const
  {
    return FirstMeshSet.count(coord) > 0 || RemeshSet.count(coord) > 0;
  }

  using iterator = std::vector<glm::ivec3>::iterator;
  using const_iterator = std::vector<glm::ivec3>::const_iterator;
  iterator begin()
  {
    EnsureUnified();
    return Queue.begin();
  }
  iterator end()
  {
    EnsureUnified();
    return Queue.end();
  }
  const_iterator begin() const
  {
    EnsureUnified();
    return Queue.begin();
  }
  const_iterator end() const
  {
    EnsureUnified();
    return Queue.end();
  }

  iterator RemoveAt(iterator it);

  /// Sort FirstMeshQ then RemeshQ independently, then refresh unified view.
  void SortByDistanceKey(glm::ivec3 focus_ground_chunk, int preferred_cy,
                         bool prefer_lower_cy, bool vertical_valid,
                         const std::function<bool(glm::ivec3)> &missing_mesh,
                         float forward_bias_k = 0.0f,
                         glm::vec2 forward_xz = glm::vec2(0.0f),
                         int focus_radius_for_tail = -1);

  /// P3: stable-partition FirstMeshQ so underfeet / just-relit nh≤2 sit first.
  void BoostJustRelitNear(glm::ivec3 focus_ground_chunk, glm::ivec2 relit_xz,
                          int max_horiz);

  void PartialSortByDistanceKey(
      glm::ivec3 focus_ground_chunk, int preferred_cy, bool prefer_lower_cy,
      bool vertical_valid,
      const std::function<bool(glm::ivec3)> &missing_mesh, size_t keep_front,
      float forward_bias_k = 0.0f, glm::vec2 forward_xz = glm::vec2(0.0f),
      int focus_radius_for_tail = -1);

  void PrioritizeChunksWithoutMesh(
      const std::function<bool(glm::ivec3)> &missing_mesh);
  void PrioritizeNearHorizontal(glm::ivec3 focus_ground_chunk, int radius_chunks);
  void PrioritizeVerticalCy(glm::ivec3 focus_ground_chunk, int radius_chunks,
                            int preferred_cy, bool prefer_lower_cy);

  /// Drop farthest remesh entries until total Size <= soft_cap. Never drops
  /// FirstMeshQ or underfeet / missing when missing_mesh is set.
  int MaybeDropFarthest(glm::ivec3 focus_ground_chunk, size_t soft_cap,
                        int min_keep_horiz = 1,
                        const std::function<bool(glm::ivec3)> &missing_mesh = {});

  void ReserveCapacity(size_t n)
  {
    FirstMeshQ.reserve(n);
    RemeshQ.reserve(n);
    FirstMeshSet.reserve(n);
    RemeshSet.reserve(n);
    Queue.reserve(n);
  }

  const std::vector<glm::ivec3> &FirstMeshQueue() const { return FirstMeshQ; }
  const std::vector<glm::ivec3> &RemeshQueue() const { return RemeshQ; }
  std::vector<glm::ivec3> &FirstMeshQueueMutable()
  {
    InvalidateUnified();
    return FirstMeshQ;
  }
  std::vector<glm::ivec3> &RemeshQueueMutable()
  {
    InvalidateUnified();
    return RemeshQ;
  }

  /// Perf-root P2: O(R²) column-index lookup instead of O(|Dirty|) scan.
  int CountWithinHorizontalRadius(glm::ivec3 center_chunk,
                                  int radius_chunks) const;

private:
  void InvalidateUnified() const { UnifiedDirty = true; }
  void EnsureUnified() const;
  void NoteColumnAdd(glm::ivec3 coord);
  void NoteColumnRemove(glm::ivec3 coord);

  std::vector<glm::ivec3> FirstMeshQ;
  std::vector<glm::ivec3> RemeshQ;
  std::unordered_set<glm::ivec3, IVec3Hash> FirstMeshSet;
  std::unordered_set<glm::ivec3, IVec3Hash> RemeshSet;
  /// Lazy concat FirstMeshQ + RemeshQ for legacy iterators.
  mutable std::vector<glm::ivec3> Queue;
  mutable bool UnifiedDirty{true};
  /// Packed (x,z) → dirty chunk count in that column (all cy).
  std::unordered_map<uint64_t, int> ColumnCounts;
};

} // namespace cutum

#endif
