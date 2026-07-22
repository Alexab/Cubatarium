#ifndef CHUNKMANAGER_H
#define CHUNKMANAGER_H

#include "World/Chunks/Chunk.h"
#include "World/Math/BlockTypes.h"
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
  FluidCellState GetFluidState(glm::ivec3 worldPos) const;
  void SetBlock(glm::ivec3 worldPos, BlockId Id);
  void SetFluidState(glm::ivec3 worldPos, FluidCellState state);
  void ClearFluidState(glm::ivec3 worldPos);
  void Clear();
  void ForEachBlock(const std::function<void(glm::ivec3, BlockId)> &fn) const;

  UChunk *GetChunk(glm::ivec3 chunk_coord);
  const UChunk *GetChunk(glm::ivec3 chunk_coord) const;
  bool HasChunk(glm::ivec3 chunk_coord) const;
  void EnsureChunk(glm::ivec3 chunk_coord);
  void RemoveChunk(glm::ivec3 chunk_coord);
  void ForEachChunk(const std::function<void(const UChunk &)> &fn) const;
  /// Cap recycled chunks retained after unload (0 = destroy immediately).
  void SetMaxFreeListChunks(size_t cap) { MaxFreeListChunks = cap; }
  size_t GetFreeListSize() const { return FreeList.size(); }

  static glm::ivec3 WorldToChunk(glm::ivec3 world_pos);
  static glm::ivec3 WorldToLocal(glm::ivec3 world_pos);

private:
  UChunk &GetOrCreateChunk(glm::ivec3 chunk_coord);

  std::unordered_map<glm::ivec3, std::unique_ptr<UChunk>, IVec3Hash> Chunks;
  std::vector<std::unique_ptr<UChunk>> FreeList;
  size_t MaxFreeListChunks{256};
};

} // namespace cutum

#endif
