#ifndef CHUNKMESHCACHE_H
#define CHUNKMESHCACHE_H

#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>
#include "BlockTypes.h"
#include "ChunkManager.h"

namespace cutum {

struct BlockInstance {
 glm::mat4 model{1.0f};
 BlockId id{BLOCK_AIR};
};

class BlockRegistry;
class BlockWorld;

class ChunkMeshCache {
public:
 void MarkAllDirty();
 void MarkAllDirtyFromWorld(const BlockWorld& world);
 void MarkDirty(glm::ivec3 chunkCoord);
 void RebuildDirtyChunks(BlockWorld& world, BlockRegistry& registry, int maxChunksPerFrame = 8);
 void RebuildAll(BlockWorld& world, BlockRegistry& registry);
 bool HasPendingDirty() const { return !dirtyChunks_.empty(); }

 const std::vector<BlockInstance>& GetInstances() const { return instances_; }

private:
 void RebuildChunk(const BlockWorld& world, BlockRegistry& registry, glm::ivec3 chunkCoord);

 std::unordered_map<glm::ivec3, std::vector<BlockInstance>, IVec3Hash> cache_;
 std::vector<glm::ivec3> dirtyChunks_;
 std::vector<BlockInstance> instances_;
 bool instancesDirty_{true};
};

}

#endif
