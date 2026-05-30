#ifndef CHUNKMESHCACHE_H
#define CHUNKMESHCACHE_H

#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>
#include "BlockTypes.h"
#include "ChunkManager.h"

namespace cutum {

struct FaceInstance {
 glm::mat4 model{1.0f};
 BlockId id{BLOCK_AIR};
};

using BlockInstance = FaceInstance;

class BlockRegistry;
class BlockWorld;

class ChunkMeshCache {
public:
 void MarkAllDirty();
 void MarkAllDirtyFromWorld(const BlockWorld& world);
 void MarkDirty(glm::ivec3 chunkCoord);
 void RemoveChunk(glm::ivec3 chunkCoord);
 void RebuildDirtyChunks(BlockWorld& world, BlockRegistry& registry, int maxChunksPerFrame = 8);
 void RebuildAll(BlockWorld& world, BlockRegistry& registry);
 bool HasPendingDirty() const { return !dirtyChunks_.empty(); }
 size_t GetInstanceCount() const { return instances_.size(); }

 const std::vector<FaceInstance>& GetFaceInstances() const { return instances_; }
 const std::vector<FaceInstance>& GetInstances() const { return instances_; }

private:
 void RebuildChunk(const BlockWorld& world, BlockRegistry& registry, glm::ivec3 chunkCoord);

 std::unordered_map<glm::ivec3, std::vector<FaceInstance>, IVec3Hash> cache_;
 std::vector<glm::ivec3> dirtyChunks_;
 std::vector<FaceInstance> instances_;
 bool instancesDirty_{true};
};

}

#endif
