#ifndef WORLDPERSISTENCE_H
#define WORLDPERSISTENCE_H

#include "World/Chunks/ChunkManager.h"
#include "World/IO/AsyncChunkIO.h"
#include "World/IO/ChunkStorageService.h"
#include "World/IO/ChunkStorageTypes.h"
#include <atomic>
#include <chrono>
#include <deque>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cutum
{

struct IVec2Hash
{
  std::size_t operator()(const glm::ivec2 &v) const noexcept
  {
    return std::hash<int64_t>{}((static_cast<int64_t>(v.x) << 32) ^
                                static_cast<uint32_t>(v.y));
  }
};

class UBlockRegistry;
class UBlockWorld;
class UWorld;

class UWorldPersistence
{
public:
  struct TerrainColumnRelightQueueInfo
  {
    bool keyed{false};
    bool priority{false};
    bool in_deque{false};
    bool deferred_visible{false};
    bool deferred_far{false};
    bool y_band_defined{false};
    int queue_index{-1};
    int queue_size{0};
    int min_world_y{0};
    int max_world_y{-1};
  };

  UWorldPersistence();

  static bool HasPersistedTerrainOnDisk(const std::string &world_folder_path);

  void EnsureChunkIoInitialized();
  void SetChunkWriteFormat(ChunkWriteFormat format);
  ChunkWriteFormat GetChunkWriteFormat() const;

  UChunkStorageService &GetChunkStorage() { return *ChunkStorage; }
  const UChunkStorageService &GetChunkStorage() const { return *ChunkStorage; }

  const std::string &GetWorldFolderPath() const { return WorldFolderPath; }
  void SetWorldFolderPath(const std::string &path)
  {
    if (WorldFolderPath == path)
    {
      return;
    }
    SaveColumnLightFlagsIfDirty();
    WorldFolderPath = path;
    LightCompleteColumns.clear();
    LightCompleteDirty = false;
    LightCompleteLoaded = false;
    if (!WorldFolderPath.empty())
    {
      LoadColumnLightFlags();
    }
  }

  void LoadUsers(UWorld &world, const std::string &file_name);
  void SaveUsers(UWorld &world, const std::string &file_name);
  void LoadWorldData(UWorld &world, const std::string &file_name);
  void SaveWorldData(UWorld &world, const std::string &file_name);

  void TickAsyncChunkIo(UWorld &world, std::size_t max_slice_applies = 0,
                        double max_apply_ms = 0.0);
  void FlushAsyncChunkIo(UWorld &world);
  bool TickDrainAsyncChunkIo(UWorld &world, int max_iterations);
  bool IsAsyncChunkIoQuiescent() const;
  void TraceAsyncChunkIoShutdownState() const;
  void AbortAsyncChunkIo();
  bool AbortAsyncChunkIoFor(std::chrono::milliseconds timeout);
  void EnqueueTerrainColumnRelight(int world_x, int world_z,
                                   bool priority = false, int min_y = 0,
                                   int max_y = -1);
  /// Admit bounded visible relight work with far-victim replacement/reserve
  /// when normal near-ring FIFO admission is backpressured.
  bool EnqueueVisibleRelight(int world_x, int world_z, int min_y, int max_y,
                             glm::ivec3 focus_ground, int max_horiz,
                             const std::vector<glm::ivec2>
                                 &protected_visible_columns,
                             uint8_t *outcome = nullptr,
                             int *victim_horiz = nullptr);
  /// Preserve an exact FirstMesh light band through surface-range selection.
  /// The key is the block-space XZ column origin used by the relight queues.
  void NoteVisibleFirstMeshRelight(glm::ivec2 world_block_key, int min_y,
                                   int max_y);
  void ClearVisibleFirstMeshRelightIfNotQueued(
      glm::ivec2 world_block_key);
  /// Compatibility entry point for renderer draw-gate callers.
  bool EnqueueVisibleDrawGateRelight(int world_x, int world_z, int min_y,
                                     int max_y, glm::ivec3 focus_ground,
                                     int max_horiz,
                                     const std::vector<glm::ivec2>
                                         &protected_visible_columns,
                                     uint8_t *outcome = nullptr,
                                     int *victim_horiz = nullptr);
  /// F3b: enqueue only when column surface still needs relight.
  bool TryEnqueueTerrainColumnRelight(UWorld &world, int world_x, int world_z,
                                      bool priority = false, int min_y = 0,
                                      int max_y = -1);
  void DeferFarRelightColumn(glm::ivec2 ground_xz, int min_y, int max_y,
                             bool priority);
  void ClearDeferredFarRelightColumn(glm::ivec2 ground_xz);
  int AdmitDeferredFarRelightColumns(UWorld &world, glm::ivec3 focus_ground,
                                     int pin_horiz);
  /// Move an already-queued world-block-key column from far to priority FIFO.
  void PromoteTerrainColumnRelight(glm::ivec2 world_block_key);
  /// Move one queued near-focus column to the priority head, preserving the
  /// active relight pin immediately ahead of it when that pin is queued.
  bool PrioritizeTerrainColumnRelight(glm::ivec2 world_block_key,
                                      bool pin_in_flight = false);
  /// Promote all pending far-FIFO columns within focus radius (block keys).
  int PromoteNearTerrainColumnRelights(glm::ivec3 focus_ground,
                                       int radius_chunks);
  void EnqueuePlayerRelight(const std::vector<glm::ivec3> &block_positions);
  void DrainRelightQueues(UWorld &world, int max_player_jobs, int max_bg_columns);
  void DrainTerrainColumnRelights(UWorld &world, int max_columns);
  int GetPendingTerrainColumnRelightCount() const;
  bool IsTerrainColumnRelightQueued(glm::ivec2 world_block_key) const;
  /// Drop queued/deferred visual relight debt for a column being evicted.
  /// Callers must invalidate its disk-light-complete flag when this abandons
  /// unfinished lighting so a later disk load recomputes it.
  int CancelTerrainColumnRelight(glm::ivec2 world_block_key);
  TerrainColumnRelightQueueInfo GetTerrainColumnRelightQueueInfo(
      glm::ivec2 world_block_key) const;
  int GetPendingPlayerRelightCount() const;
  /// Drop farthest far-FIFO columns until size <= soft_cap (priority untouched).
  int TrimFarRelightFifoFarthest(glm::ivec3 focus_ground, int soft_cap,
                                 int protect_horiz = -1);
  /// P1: miss / PromoteRelightHold column that far overflow must not pop.
  void SetRelightFifoPin(glm::ivec2 chunk_xz, bool valid);
  int TakeRelightFifoOverflowDropped();
  int TakeRelightFifoPinSaved();
  int TakeRelightFifoPriorityInsert();
  int TakeRelightFifoProtectBlock();
  void ClearPendingRelights();
  void RequestAsyncTerrainColumnLoad(UWorld &world, glm::ivec3 ground_coord);
  void RequestAsyncTerrainColumnSave(UWorld &world, glm::ivec3 ground_coord);
  void CancelAsyncTerrainColumnLoad(glm::ivec3 ground_coord);
  int CancelAsyncTerrainColumnLoadsOutsideRadius(UWorld &world,
                                                glm::ivec3 center,
                                                int radius_chunks);
  bool IsTerrainColumnDiskLoadPending(glm::ivec3 ground_coord) const;

  int LoadTerrainColumn(glm::ivec3 coord, UBlockWorld &block_world,
                        UBlockRegistry &registry, int max_height);
  void SaveTerrainColumn(glm::ivec3 ground_coord, UBlockWorld &block_world,
                         UBlockRegistry &registry, int max_height);
  /// Remove all disk slices for a ground column (used when incomplete in RAM
  /// must not leave a stale complete ocean/land file behind).
  void RemoveTerrainColumnFromDisk(glm::ivec3 ground_coord, int max_height);
  /// Clear in-memory column and delete its disk slices (load repair).
  void PurgeIncompleteTerrainColumn(UBlockWorld &block_world,
                                    glm::ivec3 ground_coord, int max_height);
  void LoadInitialTerrainColumns(UWorld &world, glm::vec3 spawn_point,
                                 int render_distance_chunks);

  /// Minetest-style lighting_complete: disk lightmap trusted for this column.
  bool IsColumnLightComplete(glm::ivec2 ground_xz) const;
  void SetColumnLightComplete(glm::ivec2 ground_xz, bool complete);
  void ClearColumnLightComplete(glm::ivec2 ground_xz);
  void LoadColumnLightFlags();
  void SaveColumnLightFlagsIfDirty();

private:
  struct PendingAsyncColumnLoadState
  {
    int remaining_results{0};
    int highest_cy_on_disk{-1};
    std::shared_ptr<std::atomic<bool>> cancellation;
    std::chrono::steady_clock::time_point requested_at{};
    double disk_discovery_ms{0.0};
    int timing_slice_count{0};
    double format_detect_ms{0.0};
    double worker_queue_ms{0.0};
    double worker_queue_max_ms{0.0};
    double file_open_ms{0.0};
    double file_read_ms{0.0};
    double file_read_max_ms{0.0};
    double disk_probe_ms{0.0};
    double disk_probe_max_ms{0.0};
    double result_wait_ms{0.0};
    double result_wait_max_ms{0.0};
    double deserialize_ms{0.0};
    double deserialize_max_ms{0.0};
    double apply_ms{0.0};
    double apply_max_ms{0.0};
    double deserialize_apply_ms{0.0};
    double deserialize_apply_max_ms{0.0};
    bool had_disk_read_failure{false};
    bool had_invalid_token{false};
    bool had_disk_light{false};
    int retry_generation{0};
  };

  static constexpr int kMaxAsyncColumnLoadRetries = 4;

  void FinalizeAsyncTerrainColumnLoad(UWorld &world, glm::ivec3 ground_coord,
                                      PendingAsyncColumnLoadState state);
  void EnqueueTerrainColumnRelightImpl(int world_x, int world_z,
                                       bool priority, int min_y, int max_y,
                                       bool visible_admission);
  void PreserveRelightFifoVictimAsDeferred(glm::ivec2 world_block_key,
                                           bool priority);
  bool PrioritizeNearestTerrainColumnRelight(UWorld &world,
                                             glm::ivec3 focus_ground,
                                             int radius_chunks, int scan_cap);

  std::unique_ptr<UAsyncChunkIO> AsyncChunkIo;
  std::unique_ptr<UChunkStorageService> ChunkStorage;
  std::unordered_map<glm::ivec3, PendingAsyncColumnLoadState, IVec3Hash>
      PendingAsyncColumnLoadSlices;
  std::unordered_map<glm::ivec3, int, IVec3Hash> PendingAsyncColumnSaveSlices;
  struct PlayerRelightRequest
  {
    std::vector<glm::ivec3> block_positions;
    int min_world_y{0};
  };
  std::deque<PlayerRelightRequest> PendingPlayerRelights;
  std::deque<glm::ivec2> PendingTerrainColumnRelights;
  std::deque<glm::ivec2> PendingTerrainColumnRelightsPriority;
  std::unordered_set<glm::ivec2, IVec2Hash> PendingTerrainColumnRelightKeys;
  /// Optional Y band per pending column (min,max); missing => full 0..MaxHeight.
  std::unordered_map<glm::ivec2, glm::ivec2, IVec2Hash>
      PendingTerrainColumnRelightYBands;
  /// Exact renderer-rejected Y band, kept separately from a coalesced column
  /// relight band so surface clamping cannot silently skip a visible slice.
  std::unordered_map<glm::ivec2, glm::ivec2, IVec2Hash>
      PendingVisibleDrawGateRelightYBands;
  /// Exact visible FirstMesh debt; these bands must not be moved to the
  /// surface slice while the corresponding missing-mesh demand is pending.
  std::unordered_map<glm::ivec2, glm::ivec2, IVec2Hash>
      PendingVisibleFirstMeshRelightYBands;
  /// Bounded exact draw-gate targets kept outside the shared FIFO until the
  /// capture dequeue can promote one directly to the head.
  std::unordered_map<glm::ivec2, glm::ivec2, IVec2Hash>
      DeferredVisibleDrawGateRelightYBands;
  /// FZ2.3-O2: last StreamingFrameEpoch when finalize_gate Capture submitted.
  std::unordered_map<glm::ivec2, uint64_t, IVec2Hash> RelightLastFinalizeEpoch_;
  struct DeferredFarRelightEntry
  {
    glm::ivec2 y_band;
    bool priority{false};
  };
  std::unordered_map<glm::ivec2, DeferredFarRelightEntry, IVec2Hash>
      DeferredFarRelightColumns;
  /// Columns whose disk lightmap is trusted (lighting_complete).
  std::unordered_set<glm::ivec2, IVec2Hash> LightCompleteColumns;
  bool LightCompleteDirty{false};
  bool LightCompleteLoaded{false};
  std::string WorldFolderPath;
  bool RelightFifoPinValid{false};
  int RelightFifoPinCx{0};
  int RelightFifoPinCz{0};
  bool RelightFifoTrimFocusValid{false};
  int RelightFifoTrimFocusCx{0};
  int RelightFifoTrimFocusCz{0};
  int RelightFifoOverflowDroppedN{0};
  int RelightFifoPinSavedN{0};
  int RelightFifoPriorityInsertN{0};
  int RelightFifoProtectBlockN{0};
  /// I10-B1: last-frame miss context for EnqueueTerrainColumnRelight pin boost.
  int RelightEnqueueMissHoriz_{-1};
  int RelightEnqueueWitnessHoldN_{0};
};

} // namespace cutum

#endif // WORLDPERSISTENCE_H
