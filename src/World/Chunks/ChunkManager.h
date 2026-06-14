#ifndef CHUNKMANAGER_H
#define CHUNKMANAGER_H

#include "World/Math/BlockTypes.h"
#include "World/Chunks/Chunk.h"
#include <functional>
#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>

namespace cutum
{

struct IVec3Hash
{
  size_t operator()(const glm::ivec3 &v) const noexcept
  {
    const size_t h1 = std::hash<int>()(v.x);
    const size_t h2 = std::hash<int>()(v.y);
    const size_t h3 = std::hash<int>()(v.z);
    return h1 ^ (h2 << 1) ^ (h3 << 2);
  }
};

class UChunkManager
{
public:
  BlockId GetBlock(glm::ivec3 worldPos) const;
  void SetBlock(glm::ivec3 worldPos, BlockId id);
  void Clear();
  void ForEachBlock(const std::function<void(glm::ivec3, BlockId)> &fn) const;

  UChunk *GetChunk(glm::ivec3 chunk_coord);
  const UChunk *GetChunk(glm::ivec3 chunk_coord) const;
  bool HasChunk(glm::ivec3 chunk_coord) const;
  void RemoveChunk(glm::ivec3 chunk_coord);
  void ForEachChunk(const std::function<void(const UChunk &)> &fn) const;

  static glm::ivec3 WorldToChunk(glm::ivec3 world_pos);
  static glm::ivec3 WorldToLocal(glm::ivec3 world_pos);

private:
  UChunk &GetOrCreateChunk(glm::ivec3 chunk_coord);

  std::unordered_map<glm::ivec3, std::unique_ptr<UChunk>, IVec3Hash> Chunks;
};

} // namespace cutum

#endif
