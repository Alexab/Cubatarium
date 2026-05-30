#ifndef CHUNKMANAGER_H
#define CHUNKMANAGER_H

#include <functional>
#include <memory>
#include <unordered_map>
#include <glm/glm.hpp>
#include "BlockTypes.h"
#include "Chunk.h"

namespace cutum {

struct IVec3Hash {
 size_t operator()(const glm::ivec3& v) const noexcept
 {
  const size_t h1 = std::hash<int>()(v.x);
  const size_t h2 = std::hash<int>()(v.y);
  const size_t h3 = std::hash<int>()(v.z);
  return h1 ^ (h2 << 1) ^ (h3 << 2);
 }
};

class ChunkManager {
public:
 BlockId GetBlock(glm::ivec3 worldPos) const;
 void SetBlock(glm::ivec3 worldPos, BlockId id);
 void Clear();
 void ForEachBlock(const std::function<void(glm::ivec3, BlockId)>& fn) const;

 Chunk* GetChunk(glm::ivec3 chunkCoord);
 const Chunk* GetChunk(glm::ivec3 chunkCoord) const;
 void ForEachChunk(const std::function<void(const Chunk&)>& fn) const;

 static glm::ivec3 WorldToChunk(glm::ivec3 worldPos);
 static glm::ivec3 WorldToLocal(glm::ivec3 worldPos);

private:
 Chunk& GetOrCreateChunk(glm::ivec3 chunkCoord);

 std::unordered_map<glm::ivec3, std::unique_ptr<Chunk>, IVec3Hash> chunks_;
};

}

#endif
