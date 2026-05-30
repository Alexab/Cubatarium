#ifndef CHUNKSTREAMER_H
#define CHUNKSTREAMER_H

#include <functional>
#include <string>
#include <unordered_set>
#include <glm/glm.hpp>
#include "BlockTypes.h"
#include "ChunkManager.h"

namespace cutum {

class BlockRegistry;
class BlockWorld;

class ChunkStreamer {
public:
 using LoadChunkFn = std::function<bool(glm::ivec3)>;
 using SaveChunkFn = std::function<void(glm::ivec3)>;
 using MarkDirtyFn = std::function<void(glm::ivec3)>;
 using UnloadChunkFn = std::function<void(glm::ivec3)>;
 using GenerateColumnFn = std::function<void(int x, int z)>;

 ChunkStreamer(BlockWorld& world, BlockRegistry& registry, uint32_t seed,
     int baseY, int maxHeight);

 void SetWorldFolder(const std::string& path);
 void SetCallbacks(LoadChunkFn loadFn, SaveChunkFn saveFn, MarkDirtyFn markDirtyFn,
     GenerateColumnFn generateColumnFn, UnloadChunkFn unloadFn = nullptr);
 void SetRenderDistance(int chunks) { renderDistance_ = chunks; }
 void SetEnabled(bool enabled) { enabled_ = enabled; }

 void Update(glm::ivec3 cameraBlockPos);

private:
 void EnsureChunkLoaded(glm::ivec3 chunkCoord);
 void UnloadDistantChunks(glm::ivec3 centerChunk);

 BlockWorld& world_;
 BlockRegistry& registry_;
 uint32_t seed_;
 int baseY_;
 int maxHeight_;
 int renderDistance_{4};
 int unloadMargin_{1};
 bool enabled_{true};
 std::string worldFolder_;

 LoadChunkFn loadChunkFn_;
 SaveChunkFn saveChunkFn_;
 MarkDirtyFn markDirtyFn_;
 UnloadChunkFn unloadChunkFn_;
 GenerateColumnFn generateColumnFn_;

 std::unordered_set<glm::ivec3, IVec3Hash> procedurallyGenerated_;
};

}

#endif
