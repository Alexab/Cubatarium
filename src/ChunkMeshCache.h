#ifndef CHUNKMESHCACHE_H
#define CHUNKMESHCACHE_H

#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>
#include "BlockTypes.h"
#include "ChunkManager.h"
#include "GreedyMeshVertex.h"
#include "RenderSettings.h"

namespace cutum {

struct Frustum;

struct FaceInstance {
 glm::mat4 model{1.0f};
 BlockId id{BLOCK_AIR};
 int faceIndex{0};
 glm::vec2 quadSize{1.0f, 1.0f};
};

using BlockInstance = FaceInstance;

struct GreedyMeshBatch {
 BlockId blockId{BLOCK_AIR};
 std::vector<GreedyMeshVertex> vertices;
 std::vector<uint32_t> indices;
};

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
 size_t GetGreedyVertexCount() const;
 uint64_t GetMeshRevision() const { return meshRevision_; }

 void UpdateVisibleInstances(const Frustum& frustum, const glm::mat4& viewProj, const glm::vec3& cameraPos);
 void SetRenderSettings(const RenderSettings& settings);

 const std::vector<FaceInstance>& GetFaceInstances() const { return instances_; }
 const std::vector<FaceInstance>& GetInstances() const { return instances_; }
 const std::vector<GreedyMeshBatch>& GetGreedyBatches() const { return greedyBatches_; }

private:
 struct ChunkGreedyMesh {
  std::vector<GreedyMeshBatch> batches;
 };

 void RebuildChunk(const BlockWorld& world, BlockRegistry& registry, glm::ivec3 chunkCoord);
 void RebuildChunkLegacy(const BlockWorld& world, BlockRegistry& registry, glm::ivec3 chunkCoord,
                         std::vector<FaceInstance>& chunkInstances);
 void RebuildFlatInstanceList(const Frustum* frustum, const glm::vec3* cameraPos);
 void RebuildFlatGreedyBatches(const Frustum* frustum, const glm::vec3* cameraPos);

 std::unordered_map<glm::ivec3, std::vector<FaceInstance>, IVec3Hash> cache_;
 std::unordered_map<glm::ivec3, ChunkGreedyMesh, IVec3Hash> greedyCache_;
 std::vector<glm::ivec3> dirtyChunks_;
 std::vector<FaceInstance> instances_;
 std::vector<GreedyMeshBatch> greedyBatches_;
 bool instancesDirty_{true};
 bool greedyBatchesDirty_{true};
 uint64_t meshRevision_{0};
 glm::mat4 lastCullVP_{1.0f};
 bool visibleListValid_{false};
 RenderSettings renderSettings_;
};

}

#endif
