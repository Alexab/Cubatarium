#include "World/Mesh/EditMeshRemeshPolicy.h"
#include <algorithm>
#include <cmath>

namespace cutum
{

std::unordered_set<glm::ivec3, detail::EditIVec3Hash>
CollectEditRemeshChunkCoords(const std::vector<glm::ivec3> &block_positions,
                             bool sync_neighbor_chunks, bool sync_light_ring)
{
  std::unordered_set<glm::ivec3, detail::EditIVec3Hash> chunk_coords;
  for (const glm::ivec3 &block_pos : block_positions)
  {
    const glm::ivec3 center = detail::EditWorldToChunk(block_pos);
    chunk_coords.insert(center);
    if (sync_neighbor_chunks || sync_light_ring)
    {
      for (const glm::ivec3 &offset : NEIGHBOR_OFFSETS)
      {
        chunk_coords.insert(
            detail::EditWorldToChunk(block_pos + offset));
      }
    }
    if (sync_light_ring)
    {
      for (int dx = -1; dx <= 1; ++dx)
      {
        for (int dy = -1; dy <= 1; ++dy)
        {
          for (int dz = -1; dz <= 1; ++dz)
          {
            chunk_coords.insert(center + glm::ivec3(dx, dy, dz));
          }
        }
      }
    }
  }
  return chunk_coords;
}

EditMeshRemeshDecision
EvaluateEditMeshRemesh(const EditMeshRemeshInput &input)
{
  EditMeshRemeshDecision out;
  if (input.BlockPositions.empty())
  {
    return out;
  }

  std::unordered_set<glm::ivec3, detail::EditIVec3Hash> center_chunks;
  for (const glm::ivec3 &block_pos : input.BlockPositions)
  {
    center_chunks.insert(detail::EditWorldToChunk(block_pos));
  }

  auto chunk_coords = CollectEditRemeshChunkCoords(
      input.BlockPositions, input.SyncNeighborChunks, input.SyncLightRing);

  if (!input.HasRegistry)
  {
    out.DirtyChunks.assign(chunk_coords.begin(), chunk_coords.end());
    return out;
  }

  const bool full_sync_rebuild =
      !input.AsyncMeshing || !input.GreedyMeshing;
  const bool hybrid_async_edit =
      input.HasRegistry && input.AsyncMeshing && input.GreedyMeshing;
  const bool sync_any = input.SyncNeighborChunks || input.SyncLightRing;
  const int cap = (std::max)(0, input.ImmediateChunkCap);

  const glm::ivec3 focus =
      detail::EditWorldToChunk(input.BlockPositions.front());
  std::vector<glm::ivec3> ordered(chunk_coords.begin(), chunk_coords.end());
  std::sort(ordered.begin(), ordered.end(),
            [&](const glm::ivec3 &a, const glm::ivec3 &b)
            {
              const int da = std::max({std::abs(a.x - focus.x),
                                       std::abs(a.y - focus.y),
                                       std::abs(a.z - focus.z)});
              const int db = std::max({std::abs(b.x - focus.x),
                                       std::abs(b.y - focus.y),
                                       std::abs(b.z - focus.z)});
              if (da != db)
              {
                return da < db;
              }
              const bool ac = center_chunks.count(a) != 0;
              const bool bc = center_chunks.count(b) != 0;
              return ac && !bc;
            });

  int immediate_n = 0;
  for (const glm::ivec3 &chunk_coord : ordered)
  {
    bool want_immediate = false;
    if (full_sync_rebuild)
    {
      want_immediate = true;
    }
    else if (hybrid_async_edit)
    {
      if (input.PreferGpuStorePatch)
      {
        // Centers only Immediate; light/face ring → Dirty for GPU upload path.
        want_immediate = center_chunks.count(chunk_coord) != 0 &&
                         immediate_n < cap;
      }
      else
      {
        want_immediate =
            center_chunks.count(chunk_coord) != 0 ||
            (sync_any && immediate_n < cap);
      }
    }
    if (want_immediate)
    {
      out.ImmediateChunks.push_back(chunk_coord);
      ++immediate_n;
    }
    else
    {
      out.DirtyChunks.push_back(chunk_coord);
    }
  }
  return out;
}

} // namespace cutum
