#pragma once

#include "World/Chunks/ChunkManager.h"
#include <cstdint>
#include <glm/glm.hpp>
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
  uint64_t Bump(glm::ivec3 coord);

private:
  mutable std::unordered_map<glm::ivec3, uint64_t, IVec3Hash> Sequences;
};

} // namespace cutum
