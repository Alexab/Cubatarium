#pragma once

#include "World/Chunks/ChunkManager.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <mutex>
#include <unordered_map>

namespace cutum
{

/// Per-chunk mesh source revision for async validation (independent per coord).
class UChunkMeshRevisionRegistry
{
public:
  uint64_t Current(glm::ivec3 coord) const;
  uint64_t Bump(glm::ivec3 coord);
  void Erase(glm::ivec3 coord);
  void Clear();

private:
  mutable std::mutex Mutex;
  std::unordered_map<glm::ivec3, uint64_t, IVec3Hash> Sequences;
};

} // namespace cutum
