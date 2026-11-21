#pragma once

#include "World/Chunks/ChunkManager.h"
#include "Core/Memory/AllocationCountingResource.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <mutex>
#include <unordered_map>

namespace cutum
{

struct ChunkGenerationToken
{
  glm::ivec3 coord{0};
  uint64_t sequence{0};

  bool IsValidFor(glm::ivec3 chunkCoord, uint64_t seq) const
  {
    return coord == chunkCoord && sequence == seq;
  }
};

class UChunkGenerationRegistry
{
public:
  ChunkGenerationToken Current(glm::ivec3 coord) const;
  ChunkGenerationToken EnsureCurrent(glm::ivec3 coord);
  uint64_t Bump(glm::ivec3 coord);
  bool Retire(glm::ivec3 coord, uint64_t expected_sequence);
  size_t LiveTokenCount() const;
  UAllocationCountingResource::Snapshot GetAllocationSnapshot() const;

private:
  mutable std::mutex Mutex;
  UAllocationCountingResource Memory;
  std::pmr::unordered_map<glm::ivec3, uint64_t, IVec3Hash> Sequences{&Memory};
  uint64_t NextSequence{0};
};

} // namespace cutum
