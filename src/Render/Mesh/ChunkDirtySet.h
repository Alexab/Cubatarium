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

  void PrioritizeChunksWithoutMesh(
      const std::function<bool(glm::ivec3)> &missing_mesh);
  void PrioritizeNearHorizontal(glm::ivec3 focus_ground_chunk, int radius_chunks);

private:
  std::vector<glm::ivec3> Queue;
  std::unordered_set<glm::ivec3, IVec3Hash> Set;
};

} // namespace cutum

#endif
