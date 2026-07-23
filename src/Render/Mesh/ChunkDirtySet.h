#ifndef CHUNKDIRTYSET_H
#define CHUNKDIRTYSET_H

#include "World/Chunks/ChunkManager.h"
#include <glm/glm.hpp>
#include <functional>
#include <unordered_set>
#include <vector>

namespace cutum
{

/// Deduped queue of chunk coords pending mesh rebuild (used by UChunkMeshCache).
/// Ordered each tick: missing-mesh class → effective horiz dist → preferred cy.
class UChunkDirtySet
{
public:
  void MarkDirty(glm::ivec3 coord);
  void MarkDirtyPriority(glm::ivec3 coord);
  void Erase(glm::ivec3 coord);
  void Clear();

  size_t GetCount() const { return Queue.size(); }
  bool empty() const { return Queue.empty(); }
  bool Contains(glm::ivec3 coord) const { return Set.find(coord) != Set.end(); }

  using iterator = std::vector<glm::ivec3>::iterator;
  using const_iterator = std::vector<glm::ivec3>::const_iterator;
  iterator begin() { return Queue.begin(); }
  iterator end() { return Queue.end(); }
  const_iterator begin() const { return Queue.begin(); }
  const_iterator end() const { return Queue.end(); }

  iterator RemoveAt(iterator it);

  /// Order: missing mesh first, then Chebyshev−forward_bias, then |cy−prefer|.
  /// forward_bias_k / forward_xz: weak motion/view bias (0 = distance only).
  void SortByDistanceKey(glm::ivec3 focus_ground_chunk, int preferred_cy,
                         bool prefer_lower_cy, bool vertical_valid,
                         const std::function<bool(glm::ivec3)> &missing_mesh,
                         float forward_bias_k = 0.0f,
                         glm::vec2 forward_xz = glm::vec2(0.0f),
                         int focus_radius_for_tail = -1);

  void PrioritizeChunksWithoutMesh(
      const std::function<bool(glm::ivec3)> &missing_mesh);
  void PrioritizeNearHorizontal(glm::ivec3 focus_ground_chunk, int radius_chunks);
  void PrioritizeVerticalCy(glm::ivec3 focus_ground_chunk, int radius_chunks,
                            int preferred_cy, bool prefer_lower_cy);

  /// Drop farthest remesh entries until Size <= soft_cap. Never drops underfeet
  /// (horiz <= min_keep_horiz) or missing-mesh entries when missing_mesh is set.
  /// Returns number of dropped coords.
  int MaybeDropFarthest(glm::ivec3 focus_ground_chunk, size_t soft_cap,
                        int min_keep_horiz = 1,
                        const std::function<bool(glm::ivec3)> &missing_mesh = {});

  void ReserveCapacity(size_t n)
  {
    Queue.reserve(n);
    Set.reserve(n);
  }

private:
  std::vector<glm::ivec3> Queue;
  std::unordered_set<glm::ivec3, IVec3Hash> Set;
};

} // namespace cutum

#endif
