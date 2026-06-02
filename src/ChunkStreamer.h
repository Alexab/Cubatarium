#ifndef CHUNKSTREAMER_H
#define CHUNKSTREAMER_H

#include <functional>
#include <string>
#include <unordered_set>
#include <vector>
#include <glm/glm.hpp>
#include "BlockTypes.h"
#include "ChunkManager.h"
#include "PlayerCapsule.h"

namespace cutum {

class BlockRegistry;
class BlockWorld;

struct StreamingFrameStats {
 void Reset()
 {
  loadsThisFrame = 0;
  unloadsThisFrame = 0;
  savesThisFrame = 0;
  loadedCoords.clear();
  unloadedCoords.clear();
 }

 int loadsThisFrame{0};
 int unloadsThisFrame{0};
 int savesThisFrame{0};
 std::vector<glm::ivec3> loadedCoords;
 std::vector<glm::ivec3> unloadedCoords;
};

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
 void SetMaxLoadOpsPerFrame(int value) { maxLoadOpsPerFrame_ = value; }
 void SetMaxUnloadOpsPerFrame(int value) { maxUnloadOpsPerFrame_ = value; }

 /// Load chunks around feet for collision — no save/unload.
 void EnsureCollisionChunks(glm::ivec3 feetBlockPos);

 /// Full streaming pass after movement: load/unload with per-frame budget.
 void Update(glm::ivec3 cameraBlockPos, const glm::vec3& eyePos, const PlayerCapsule& cap);

 const StreamingFrameStats& GetLastFrameStats() const { return lastFrameStats_; }

private:
 bool EnsureChunkLoaded(glm::ivec3 chunkCoord);
 void UnloadDistantChunks(glm::ivec3 centerChunk, glm::ivec3 feetBlockPos,
     const glm::vec3& eyePos, const PlayerCapsule& cap);
 bool ShouldKeepChunkLoaded(glm::ivec3 chunkCoord, glm::ivec3 feetBlockPos,
     const glm::vec3& eyePos, const PlayerCapsule& cap) const;

 BlockWorld& world_;
 BlockRegistry& registry_;
 uint32_t seed_;
 int baseY_;
 int maxHeight_;
 int renderDistance_{4};
 int unloadMargin_{1};
 int maxLoadOpsPerFrame_{4};
 int maxUnloadOpsPerFrame_{2};
 bool enabled_{true};
 std::string worldFolder_;

 LoadChunkFn loadChunkFn_;
 SaveChunkFn saveChunkFn_;
 MarkDirtyFn markDirtyFn_;
 UnloadChunkFn unloadChunkFn_;
 GenerateColumnFn generateColumnFn_;

 std::unordered_set<glm::ivec3, IVec3Hash> procedurallyGenerated_;
 StreamingFrameStats lastFrameStats_;
};

}

#endif
