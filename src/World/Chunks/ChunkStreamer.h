#ifndef CHUNKSTREAMER_H
#define CHUNKSTREAMER_H

#include "Creatures/Player/PlayerCapsule.h"
#include "World/Chunks/ChunkLoadPriority.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Math/BlockTypes.h"
#include <algorithm>
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
    asyncQueuedThisFrame = 0;
    diskCompleteThisFrame = 0;
    genCommitThisFrame = 0;
    ringGateBlocked = 0;
    nearLoadSkipped = 0;
    loadCandidates = 0;
    loadedCoords.clear();
    unloadedCoords.clear();
  }

  int loadsThisFrame{0};
  int unloadsThisFrame{0};
  int savesThisFrame{0};
  /// Async column requests issued this frame (EnsureChunkLoaded queued work).
  int asyncQueuedThisFrame{0};
  /// Era25: sync Ensure completed via OnLoadChunk (disk-hit honesty).
  int diskCompleteThisFrame{0};
  /// Era25: async gen commits applied this frame (scheduler Tick).
  int genCommitThisFrame{0};
  /// Candidates that failed RingPrerequisitesMet this frame.
  int ringGateBlocked{0};
  /// Candidates skipped by NearLoadRadius clamp.
  int nearLoadSkipped{0};
  int loadCandidates{0};
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
  using UnloadColumnFn = std::function<void(glm::ivec3 ground, int max_cy)>;
  using GenerateColumnFn = std::function<void(int x, int z)>;
  using RequestAsyncChunkFn = std::function<void(glm::ivec3, int priority)>;
  using IsChunkCommittedFn = std::function<bool(glm::ivec3)>;
  using IsColumnPendingFn = std::function<bool(glm::ivec3)>;
  /// True while column awaits first light before mesh (visual hole).
  using IsColumnPendingLightFn = std::function<bool(glm::ivec3)>;

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
  void SetColumnPendingLightCallback(IsColumnPendingLightFn fn);
  void SetGenerationLightingHooks(std::function<void(bool)> defer_relight,
                                  std::function<void(glm::ivec3)> relight_column);
  /// When >= 0, Update/Prefetch only load within Chebyshev radius of load center.
  /// Negative = unlimited (default). Collision-urgent feet loads ignore this.
  void SetNearLoadRadius(int radius_chunks)
  {
    NearLoadRadius = radius_chunks;
  }
  void NotifyChunkCommitted(glm::ivec3 chunkCoord);
  void MarkPersistedColumnsFromWorld();
  void SetRenderDistance(int chunks)
  {
    VisualRenderDistance = std::max(1, chunks);
    KeepRenderDistance =
        std::max(VisualRenderDistance, VisualRenderDistance + KeepPrefetchMargin);
  }
  void SetVisualRenderDistance(int chunks)
  {
    VisualRenderDistance = std::max(1, chunks);
  }
  void SetKeepPrefetchMargin(int margin)
  {
    KeepPrefetchMargin = std::max(0, margin);
    KeepRenderDistance =
        std::max(VisualRenderDistance, VisualRenderDistance + KeepPrefetchMargin);
  }
  void SetKeepRenderDistance(int chunks)
  {
    KeepRenderDistance = std::max(VisualRenderDistance, chunks);
  }
  int GetVisualRenderDistance() const { return VisualRenderDistance; }
  int GetKeepRenderDistance() const { return KeepRenderDistance; }
  void SetMaxTerrainHeight(int height) { MaxHeight = height; }
  void SetEnabled(bool enabled) { Enabled = enabled; }
  void SetMaxLoadOpsPerFrame(int value) { MaxLoadOpsPerFrame = value; }
  void SetMaxKeepPrefetchOpsPerFrame(int value)
  {
    MaxKeepPrefetchOpsPerFrame = std::max(0, value);
  }
  void SetMaxUnloadOpsPerFrame(int value)
  {
    MaxUnloadOpsPerFrame = value;
    EffectiveUnloadOpsPerFrame = value;
  }
  void SetUnloadColumnCallback(UnloadColumnFn fn)
  {
    OnUnloadColumn = std::move(fn);
  }
  void SetEffectiveUnloadOpsPerFrame(int value)
  {
    EffectiveUnloadOpsPerFrame = value;
  }
  void SetViewForward(glm::vec3 forward_xz) { ViewForwardXz = forward_xz; }
  void SetRingGateEnabled(bool enabled) { RingGateEnabled = enabled; }
  void SetCollisionUrgentRing(glm::ivec3 feet_chunk, int radius_chunks,
                              bool urgent);

  bool IsPositionInActiveRing(const glm::vec3 &worldPos, glm::ivec3 feetBlockPos,
                            const glm::vec3 &eyePos,
                            const PlayerCapsule &cap) const;

  /// Load chunks around feet for collision — no save/unload.
  void EnsureCollisionChunks(glm::ivec3 feetBlockPos);
  bool IsCollisionReady(glm::ivec3 feetBlockPos, int radiusChunks) const;

  /// Full streaming pass after Movement: load/unload with per-frame budget.
  void Update(glm::ivec3 cameraBlockPos, const glm::vec3 &eyePos,
              const PlayerCapsule &cap);
  void PrefetchAhead(glm::ivec3 feet_chunk, glm::vec3 view_forward_xz,
                     float movement_speed, float speed_threshold,
                     int *out_ops = nullptr);
  void PrefetchKeepShell(glm::ivec3 feet_chunk, int max_ops,
                         int *out_ops = nullptr);

  const StreamingFrameStats &GetLastFrameStats() const
  {
    return LastFrameStats;
  }

private:
  bool EnsureChunkLoaded(glm::ivec3 chunkCoord, bool forceSync = false,
                         bool *out_async_queued = nullptr);
  bool AdvanceTerrainColumnGeneration(glm::ivec3 chunkCoord, int max_sub_columns,
                                      bool only_empty_columns);
  bool IsTerrainChunkCompleteCached(glm::ivec3 groundCoord);
  void InvalidateTerrainCompleteCache(glm::ivec3 groundCoord);
  void UnloadDistantChunks(glm::ivec3 centerChunk, glm::ivec3 feetBlockPos,
                           const glm::vec3 &eyePos, const PlayerCapsule &cap);
  bool ShouldKeepChunkLoaded(glm::ivec3 chunkCoord, glm::ivec3 feetBlockPos,
                             const glm::vec3 &eyePos,
                             const PlayerCapsule &cap) const;
  int ChunkHorizontalDistance(glm::ivec3 groundCoord) const;
  int ChunkLoadPriorityFor(glm::ivec3 groundCoord) const;
  /// Inward ring gate. When allow_pending_inward, a queued/pending neighbor
  /// counts as ready (used by PrefetchAhead so deep steps are not stuck
  /// waiting for commit of the previous step).
  bool RingPrerequisitesMet(glm::ivec3 coord,
                            bool allow_pending_inward = false);

  UBlockWorld &World;
  UBlockRegistry &Registry;
  uint32_t Seed;
  int BaseY;
  int MaxHeight;
  int VisualRenderDistance{4};
  int KeepRenderDistance{6};
  int KeepPrefetchMargin{2};
  int UnloadMargin{1};
  int MaxLoadOpsPerFrame{4};
  int MaxKeepPrefetchOpsPerFrame{2};
  int MaxUnloadOpsPerFrame{2};
  int EffectiveUnloadOpsPerFrame{2};
  bool Enabled{true};
  std::string WorldFolder;

  LoadChunkFn OnLoadChunk;
  SaveChunkFn OnSaveChunk;
  MarkDirtyFn OnMarkDirty;
  UnloadChunkFn OnUnloadChunk;
  UnloadColumnFn OnUnloadColumn;
  GenerateColumnFn OnGenerateColumn;
  RequestAsyncChunkFn OnRequestAsyncChunk;
  IsChunkCommittedFn OnIsChunkCommitted;
  IsColumnPendingFn OnIsColumnPending;
  IsColumnPendingLightFn OnIsColumnPendingLight;
  std::function<void(bool)> OnSetLightingRelightDeferred;
  std::function<void(glm::ivec3)> OnRelightTerrainColumn;
  bool AsyncGeneration{false};
  bool RingGateEnabled{false};
  int NearLoadRadius{-1};
  bool CollisionUrgent{false};
  glm::ivec3 CollisionUrgentCenter{0};
  int CollisionUrgentRadius{0};
  glm::vec3 ViewForwardXz{0.0f, 0.0f, 1.0f};
  ChunkLoadPriorityParams PriorityParams;

  std::unordered_set<glm::ivec3, IVec3Hash> ProcedurallyGenerated;
  struct ColumnGenState
  {
    int cursor{0};
    bool onlyEmptyColumns{false};
  };
  std::unordered_map<glm::ivec3, ColumnGenState, IVec3Hash> ColumnGenStates;
  std::unordered_map<glm::ivec3, bool, IVec3Hash> TerrainCompleteCache;
  glm::ivec3 LoadPriorityCenter{0};
  StreamingFrameStats LastFrameStats;
};

} // namespace cutum

#endif
