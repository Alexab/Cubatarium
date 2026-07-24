#include "Render/Mesh/ChunkMeshRevisionRegistry.h"

namespace cutum
{

uint64_t UChunkMeshRevisionRegistry::Current(glm::ivec3 coord) const
{
  std::lock_guard<std::mutex> lock(Mutex);
  const auto it = Sequences.find(coord);
  return it == Sequences.end() ? 0 : it->second;
}

uint64_t UChunkMeshRevisionRegistry::Bump(glm::ivec3 coord)
{
  std::lock_guard<std::mutex> lock(Mutex);
  return ++Sequences[coord];
}

void UChunkMeshRevisionRegistry::Erase(glm::ivec3 coord)
{
  std::lock_guard<std::mutex> lock(Mutex);
  Sequences.erase(coord);
}

void UChunkMeshRevisionRegistry::Clear()
{
  std::lock_guard<std::mutex> lock(Mutex);
  Sequences.clear();
}

} // namespace cutum
