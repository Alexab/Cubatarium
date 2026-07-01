#ifndef CHUNKSTREAMER_H
#define CHUNKSTREAMER_H

#include "Creatures/Player/PlayerCapsule.h"
#include "World/Chunks/ChunkLoadPriority.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Math/BlockTypes.h"
#include <functional>
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cutum
{

class UBlockRegistry;
class UBlockWorld;

struct StreamingFrameStats
{
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

class UChunkStreamer
{
public:
  using LoadChunkFn = std::function<bool(glm::ivec3)>;
  using SaveChunkFn = std::function<void(glm::ivec3)>;
  using MarkDirtyFn = std::function<void(glm::ivec3)>;
  using UnloadChunkFn = std::function<void(glm::ivec3)>;
  using GenerateColumnFn = std::function<void(int x, int z)>;
  using RequestAsyncChunkFn = std::function<void(glm::ivec3, int priority)>;
  using IsChunkCommittedFn = std::function<bool(glm::ivec3)>;
  using IsColumnPendingFn = std::function<bool(glm::ivec3)>;

  UChunkStreamer(UBlockWorld &world, UBlockRegistry &registry, uint32_t Seed,
                 int baseY, int MaxHeight);

  void SetWorldFolder(const std::string &path);
  void SetCallbacks(LoadChunkFn loadFn, SaveChunkFn saveFn,
                    MarkDirtyFn markDirtyFn, GenerateColumnFn generateColumnFn,
                    UnloadChunkFn unloadFn = nullptr);
  void SetAsyncGeneration(bool enabled) { AsyncGeneration = enabled; }
  void SetAsyncCallbacks(RequestAsyncChunkFn requestFn,
                         IsChunkCommittedFn isCommittedFn);
  void SetColumnPendingCallback(IsColumnPendingFn fn);
  void NotifyChunkCommitted(glm::ivec3 chunkCoord);
  void MarkPersistedColumnsFromWorld();
  void SetRenderDistance(int chunks) { RenderDistance = chunks; }
  void SetMaxTerrainHeight(int height) { MaxHeight = height; }
  void SetEnabled(bool enabled) { Enabled = enabled; }
  void SetMaxLoadOpsPerFrame(int value) { MaxLoadOpsPerFrame = value; }
  void SetMaxUnloadOpsPerFrame(int value) { MaxUnloadOpsPerFrame = value; }
  void SetViewForward(glm::vec3 forward_xz) { ViewForwardXz = forward_xz; }
  void SetRingGateEnabled(bool enabled) { RingGateEnabled = enabled; }

  bool IsPositionInActiveRing(const glm::vec3 &worldPos, glm::ivec3 feetBlockPos,
                            const glm::vec3 &eyePos,
                            const PlayerCapsule &cap) const;

  /// Load chunks around feet for collision — no save/unload.
  void EnsureCollisionChunks(glm::ivec3 feetBlockPos);

  /// Full streaming pass after Movement: load/unload with per-frame budget.
  void Update(glm::ivec3 cameraBlockPos, const glm::vec3 &eyePos,
              const PlayerCapsule &cap);
  void PrefetchAhead(glm::ivec3 feet_chunk, glm::vec3 view_forward_xz,
                     float movement_speed, float speed_threshold);

  const StreamingFrameStats &GetLastFrameStats() const
  {
    return LastFrameStats;
  }

private:
  bool EnsureChunkLoaded(glm::ivec3 chunkCoord, bool forceSync = false);
  bool IsTerrainChunkCompleteCached(glm::ivec3 groundCoord);
  void InvalidateTerrainCompleteCache(glm::ivec3 groundCoord);
  void UnloadDistantChunks(glm::ivec3 centerChunk, glm::ivec3 feetBlockPos,
                           const glm::vec3 &eyePos, const PlayerCapsule &cap);
  bool ShouldKeepChunkLoaded(glm::ivec3 chunkCoord, glm::ivec3 feetBlockPos,
                             const glm::vec3 &eyePos,
                             const PlayerCapsule &cap) const;
  int ChunkHorizontalDistance(glm::ivec3 groundCoord) const;
  int ChunkLoadPriorityFor(glm::ivec3 groundCoord) const;
  bool RingPrerequisitesMet(glm::ivec3 coord);

  UBlockWorld &World;
  UBlockRegistry &Registry;
  uint32_t Seed;
  int BaseY;
  int MaxHeight;
  int RenderDistance{4};
  int UnloadMargin{1};
  int MaxLoadOpsPerFrame{4};
  int MaxUnloadOpsPerFrame{2};
  bool Enabled{true};
  std::string WorldFolder;

  LoadChunkFn OnLoadChunk;
  SaveChunkFn OnSaveChunk;
  MarkDirtyFn OnMarkDirty;
  UnloadChunkFn OnUnloadChunk;
  GenerateColumnFn OnGenerateColumn;
  RequestAsyncChunkFn OnRequestAsyncChunk;
  IsChunkCommittedFn OnIsChunkCommitted;
  IsColumnPendingFn OnIsColumnPending;
  bool AsyncGeneration{false};
  bool RingGateEnabled{false};
  glm::vec3 ViewForwardXz{0.0f, 0.0f, 1.0f};
  ChunkLoadPriorityParams PriorityParams;

  std::unordered_set<glm::ivec3, IVec3Hash> ProcedurallyGenerated;
  std::unordered_map<glm::ivec3, bool, IVec3Hash> TerrainCompleteCache;
  glm::ivec3 LoadPriorityCenter{0};
  StreamingFrameStats LastFrameStats;
};

} // namespace cutum

#endif
