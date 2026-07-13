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
