#pragma once

#include "World/Chunks/Chunk.h"
#include "World/Math/GridMath.h"
#include <glm/glm.hpp>
#include <unordered_set>
#include <vector>

namespace cutum
{

struct EditMeshRemeshDecision
{
  std::vector<glm::ivec3> ImmediateChunks;
  std::vector<glm::ivec3> DirtyChunks;
};

struct EditMeshRemeshInput
{
  std::vector<glm::ivec3> BlockPositions;
  bool SyncNeighborChunks{false};
  bool SyncLightRing{false};
  bool AsyncMeshing{true};
  bool GreedyMeshing{true};
  bool HasRegistry{true};
  /// Max sync Immediate remeshes for hybrid async edit (desktop default 9).
  int ImmediateChunkCap{9};
  /// When true (GPU store bind): only center chunks Immediate; ring is Dirty
  /// priority for upload/MDI patch instead of sync greedy storm.
  bool PreferGpuStorePatch{false};
};

namespace detail
{

struct EditIVec3Hash
{
  size_t operator()(const glm::ivec3 &v) const noexcept
  {
    const size_t h1 = std::hash<int>()(v.x);
    const size_t h2 = std::hash<int>()(v.y);
    const size_t h3 = std::hash<int>()(v.z);
    return h1 ^ (h2 << 1) ^ (h3 << 2);
  }
};

inline glm::ivec3 EditWorldToChunk(glm::ivec3 world_pos)
{
  return glm::ivec3(FloorDiv(world_pos.x, CHUNK_SIZE),
                    FloorDiv(world_pos.y, CHUNK_SIZE),
                    FloorDiv(world_pos.z, CHUNK_SIZE));
}

} // namespace detail

/// Pure policy: which edit-touched chunks remesh Immediate vs Dirty.
EditMeshRemeshDecision
EvaluateEditMeshRemesh(const EditMeshRemeshInput &input);

/// Collect unique chunk coords for edit remesh (centers + optional rings).
std::unordered_set<glm::ivec3, detail::EditIVec3Hash>
CollectEditRemeshChunkCoords(const std::vector<glm::ivec3> &block_positions,
                             bool sync_neighbor_chunks, bool sync_light_ring);

} // namespace cutum
