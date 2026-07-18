#include "Render/Mesh/ChunkDirtySet.h"

#include <algorithm>
#include <utility>

namespace cutum
{

void UChunkDirtySet::MarkDirty(glm::ivec3 coord)
{
  if (!Set.insert(coord).second)
  {
    return;
  }
  Queue.push_back(coord);
}

void UChunkDirtySet::MarkDirtyPriority(glm::ivec3 coord)
{
  if (Set.count(coord))
  {
    Queue.erase(std::remove(Queue.begin(), Queue.end(), coord), Queue.end());
  }
  else
  {
    Set.insert(coord);
  }
  Queue.insert(Queue.begin(), coord);
}

void UChunkDirtySet::Erase(glm::ivec3 coord)
{
  if (!Set.erase(coord))
  {
    return;
  }
  Queue.erase(std::remove(Queue.begin(), Queue.end(), coord), Queue.end());
}

void UChunkDirtySet::Clear()
{
  Queue.clear();
  Set.clear();
}

UChunkDirtySet::iterator UChunkDirtySet::RemoveAt(iterator it)
{
  Set.erase(*it);
  return Queue.erase(it);
}

void UChunkDirtySet::PrioritizeNearHorizontal(glm::ivec3 focus_ground_chunk,
                                              int radius_chunks)
{
  if (Queue.size() < 2 || radius_chunks < 0)
  {
    return;
  }
  std::vector<glm::ivec3> near_chunks;
  std::vector<glm::ivec3> far_chunks;
  near_chunks.reserve(Queue.size());
  far_chunks.reserve(Queue.size());
  for (const glm::ivec3 &coord : Queue)
  {
    const int dx = std::abs(coord.x - focus_ground_chunk.x);
    const int dz = std::abs(coord.z - focus_ground_chunk.z);
    if (std::max(dx, dz) <= radius_chunks)
    {
      near_chunks.push_back(coord);
    }
    else
    {
      far_chunks.push_back(coord);
    }
  }
  if (near_chunks.empty() || far_chunks.empty())
  {
    return;
  }
  Queue.clear();
  Queue.insert(Queue.end(), near_chunks.begin(), near_chunks.end());
  Queue.insert(Queue.end(), far_chunks.begin(), far_chunks.end());
}

void UChunkDirtySet::PrioritizeVerticalCy(glm::ivec3 focus_ground_chunk,
                                          int radius_chunks, int preferred_cy,
                                          bool prefer_lower_cy)
{
  if (Queue.size() < 2)
  {
    return;
  }
  auto vert_less = [preferred_cy, prefer_lower_cy](const glm::ivec3 &a,
                                                   const glm::ivec3 &b)
  {
    if (prefer_lower_cy)
    {
      return a.y < b.y;
    }
    const int da = std::abs(a.y - preferred_cy);
    const int db = std::abs(b.y - preferred_cy);
    return da < db;
  };
  size_t near_count = 0;
  for (const glm::ivec3 &coord : Queue)
  {
    const int dx = std::abs(coord.x - focus_ground_chunk.x);
    const int dz = std::abs(coord.z - focus_ground_chunk.z);
    if (radius_chunks >= 0 && std::max(dx, dz) <= radius_chunks)
    {
      ++near_count;
    }
    else
    {
      break;
    }
  }
  if (near_count > 1)
  {
    std::stable_sort(Queue.begin(), Queue.begin() + static_cast<std::ptrdiff_t>(near_count),
                     vert_less);
  }
  if (Queue.size() > near_count + 1)
  {
    std::stable_sort(Queue.begin() + static_cast<std::ptrdiff_t>(near_count),
                     Queue.end(), vert_less);
  }
}

void UChunkDirtySet::PrioritizeChunksWithoutMesh(
    const std::function<bool(glm::ivec3)> &missing_mesh)
{
  if (Queue.size() < 2)
  {
    return;
  }
  std::vector<glm::ivec3> prioritized;
  prioritized.reserve(Queue.size());
  std::vector<glm::ivec3> deferred;
  deferred.reserve(Queue.size());
  for (glm::ivec3 coord : Queue)
  {
    if (missing_mesh(coord))
    {
      prioritized.push_back(coord);
    }
    else
    {
      deferred.push_back(coord);
    }
  }
  if (prioritized.empty() || deferred.empty())
  {
    return;
  }
  Queue.clear();
  Queue.insert(Queue.end(), prioritized.begin(), prioritized.end());
  Queue.insert(Queue.end(), deferred.begin(), deferred.end());
}

} // namespace cutum
