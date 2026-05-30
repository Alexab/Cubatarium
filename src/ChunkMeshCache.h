#ifndef CHUNKMESHCACHE_H
#define CHUNKMESHCACHE_H

#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>
#include "BlockTypes.h"
#include "ChunkManager.h"
#include "RenderSettings.h"

namespace cutum {

struct Frustum;

struct FaceInstance {
 glm::mat4 model{1.0f};
 BlockId id{BLOCK_AIR};
 glm::vec4 atlasUV{0.0f, 0.0f, 1.0f / 6.0f, 1.0f};
 glm::vec2 quadSize{1.0f, 1.0f};
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
 uint64_t GetMeshRevision() const { return meshRevision_; }

 void UpdateVisibleInstances(const Frustum& frustum, const glm::mat4& viewProj);
 void SetRenderSettings(const RenderSettings& settings);

 const std::vector<FaceInstance>& GetFaceInstances() const { return instances_; }
 const std::vector<FaceInstance>& GetInstances() const { return instances_; }

private:
 void RebuildChunk(const BlockWorld& world, BlockRegistry& registry, glm::ivec3 chunkCoord);
 void RebuildChunkLegacy(const BlockWorld& world, BlockRegistry& registry, glm::ivec3 chunkCoord,
                         std::vector<FaceInstance>& chunkInstances);
 void RebuildFlatInstanceList(const Frustum* frustum);

 std::unordered_map<glm::ivec3, std::vector<FaceInstance>, IVec3Hash> cache_;
 std::vector<glm::ivec3> dirtyChunks_;
 std::vector<FaceInstance> instances_;
 bool instancesDirty_{true};
 uint64_t meshRevision_{0};
 glm::mat4 lastCullVP_{1.0f};
 bool visibleListValid_{false};
 RenderSettings renderSettings_;
};

}

#endif
