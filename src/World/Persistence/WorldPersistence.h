#include "Core/Memory/MemberCallbackStorage.h"
#ifndef WORLDPERSISTENCE_H
#define WORLDPERSISTENCE_H

#include "World/Chunks/ChunkManager.h"
#include "World/IO/AsyncChunkIO.h"
#include "World/IO/ChunkStorageService.h"
#include "World/IO/ChunkStorageTypes.h"
#include "World/IO/DiskAirCancellationProbe.h"
#include "World/IO/DiskReadFailureProbe.h"
#include "World/IO/TerrainSliceLoad.h"
#include "World/IO/ColumnSaveReceipt.h"
#include "World/Lighting/SectionLightRestoreQueue.h"
#include <atomic>
#include <chrono>
#include <deque>
#include <glm/glm.hpp>
#include <memory>
#include <optional>
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
    return std::hash<uint64_t>{}((uint64_t{static_cast<uint32_t>(v.x)} << 32) |
                                 static_cast<uint32_t>(v.y));
  }
};

class UCore;
class UBlockRegistry;
class UBlockWorld;
class UWorld;
struct BlockDefinitionCatalog;

struct AsyncChunkIoTickMetrics
{
  double tick_wall_ms{0.0};
  double unattributed_ms{0.0};
  double light_flags_result_drain_ms{0.0};
  double light_flags_result_queue_mutex_wait_ms{0.0};
  double light_flags_result_queue_mutex_held_ms{0.0};
  std::size_t light_flags_result_count{0};
  double queue_snapshot_ms{0.0};
  double discard_cancelled_ms{0.0};
  double result_selection_ms{0.0};
  double result_selection_mutex_wait_ms{0.0};
  double result_selection_mutex_held_ms{0.0};
  double result_processing_ms{0.0};
  // Nested in result_processing_ms; measures only block-buffer/chunk mutation.
  double world_apply_ms{0.0};
  double column_finalize_ms{0.0};
  double result_requeue_ms{0.0};
  double result_requeue_mutex_wait_ms{0.0};
  double result_requeue_mutex_held_ms{0.0};
  double load_result_push_mutex_wait_ms{0.0};
  double load_result_push_mutex_held_ms{0.0};
  double load_result_push_mutex_wait_max_ms{0.0};
  double load_result_push_mutex_held_max_ms{0.0};
  double save_drain_ms{0.0};
  double light_flags_save_ms{0.0};
  double light_flags_snapshot_ms{0.0};
  double light_flags_enqueue_ms{0.0};
  std::size_t light_flags_complete_columns_n{0};
  uint64_t light_flags_saved_revision{0};
  uint64_t light_flags_last_result_revision{0};
  int light_flags_last_result_success{-1};
  double light_flags_worker_queue_wait_ms{0.0};
  double light_flags_worker_service_ms{0.0};
  uint64_t load_result_push_n{0};
  std::size_t cancelled_discard_n{0};
  std::size_t ready_loads_before_n{0};
  std::size_t selected_loads_n{0};
  std::size_t processed_loads_n{0};
  std::size_t requeued_loads_n{0};
  std::size_t applied_slices_n{0};
  std::size_t saves_processed_n{0};
  std::size_t load_pending_jobs_n{0};
  std::size_t load_active_jobs_n{0};
  std::size_t load_workers_n{0};
  std::size_t background_pending_jobs_n{0};
  std::size_t background_active_jobs_n{0};
  std::size_t background_workers_n{0};
  std::size_t load_result_queue_depth_n{0};
  std::size_t save_result_queue_depth_n{0};
  bool apply_time_budget_hit{false};
};

class UWorldPersistence
{
public:
  MemberCallbackStorageSnapshot InspectStoredLightObserver(bool known) const noexcept;

  template<class Inspector> void InspectCatalogDomain(Inspector &out) const {
    out.Io(AsyncChunkIo.get());out.BlockCatalog(LightSignatureCatalog);
    for(const auto &entry:UnloadSaveReceipts)out.BlockCatalog(entry.second.catalog);
  }
  struct TerrainColumnRelightQueueInfo
  {
    bool keyed{false};
    bool priority{false};
    bool in_deque{false};
    bool deferred_visible{false};
    bool deferred_far{false};
    bool boundary_dependency{false};
    bool y_band_defined{false};
    int queue_index{-1};
    int queue_size{0};
    int min_world_y{0};
    int max_world_y{-1};
  };

  explicit UWorldPersistence(UWorld *world_context = nullptr);

  static bool HasPersistedTerrainOnDisk(const std::string &world_folder_path);

  void EnsureChunkIoInitialized();
  void SetChunkWriteFormat(ChunkWriteFormat format);
  ChunkWriteFormat GetChunkWriteFormat() const;

  UChunkStorageService &GetChunkStorage() { return *ChunkStorage; }
  const UChunkStorageService &GetChunkStorage() const { return *ChunkStorage; }

  const std::string &GetWorldFolderPath() const { return WorldFolderPath; }
  void SetWorldFolderPath(const std::string &path);

  void LoadUsers(UWorld &world, const std::string &file_name);
  void SaveUsers(UWorld &world, const std::string &file_name);
  void LoadWorldData(UWorld &world, const std::string &file_name);
  void SaveWorldData(UWorld &world, const std::string &file_name);

  AsyncChunkIoTickMetrics TickAsyncChunkIo(UWorld &world,
                                           std::size_t max_slice_applies = 0,
                                           double max_apply_ms = 0.0,
                                           std::optional<std::size_t> terrain_limit = std::nullopt);
  void FlushAsyncChunkIo(UWorld &world);
  bool TickDrainAsyncChunkIo(UWorld &world, int max_iterations);
  bool IsAsyncChunkIoQuiescent() const;
  void TraceAsyncChunkIoShutdownState() const;
  void AbortAsyncChunkIo(UWorld &world);
  bool AbortAsyncChunkIoFor(UWorld &world, std::chrono::milliseconds timeout);
  void EnqueueTerrainColumnRelight(int world_x, int world_z,
                                   bool priority, int min_y,
                                   int max_y,
                                   const char *producer = nullptr,
                                   UWorld *world_context = nullptr);
  /// Queue a relight because an installed horizontal boundary light changed.
  /// This dependency is real work even when the target has no existing visual
  /// dark-face witness, so it bypasses only the settled/no-op admission guard.
  void EnqueueBoundaryDependencyRelight(UWorld &world, int world_x, int world_z,
                                        int min_y, int max_y);
  /// Admit bounded visible relight work with far-victim replacement/reserve
  /// when normal near-ring FIFO admission is backpressured.
  bool EnqueueVisibleRelight(
      int world_x, int world_z, int min_y, int max_y, glm::ivec3 focus_ground,
      int max_horiz, const std::vector<glm::ivec2> &protected_visible_columns,
      uint8_t *outcome = nullptr, int *victim_horiz = nullptr,
      const char *producer = nullptr);
  /// Preserve an exact FirstMesh light band through surface-range selection.
  /// The key is the block-space XZ column origin used by the relight queues.
  void NoteVisibleFirstMeshRelight(glm::ivec2 world_block_key, int min_y,
                                   int max_y);
  void ClearVisibleFirstMeshRelightIfNotQueued(glm::ivec2 world_block_key);
  /// Compatibility entry point for renderer draw-gate callers.
  bool EnqueueVisibleDrawGateRelight(
      int world_x, int world_z, int min_y, int max_y, glm::ivec3 focus_ground,
      int max_horiz, const std::vector<glm::ivec2> &protected_visible_columns,
      uint8_t *outcome = nullptr, int *victim_horiz = nullptr,
      const char *producer = nullptr);
  /// F3b: enqueue only when column surface still needs relight.
  bool TryEnqueueTerrainColumnRelight(UWorld &world, int world_x, int world_z,
                                      bool priority, int min_y,
                                      int max_y);
  void DeferFarRelightColumn(glm::ivec2 ground_xz, int min_y, int max_y,
                             bool priority, const char *reason = nullptr);
  void ClearDeferredFarRelightColumn(glm::ivec2 ground_xz,
                                     const char *reason = nullptr);
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
  void DrainRelightQueues(UWorld &world, int max_player_jobs,
                          int max_bg_columns);
  void DrainTerrainColumnRelights(UWorld &world, int max_columns);
  int GetPendingTerrainColumnRelightCount() const;
  bool IsTerrainColumnRelightQueued(glm::ivec2 world_block_key) const;
  /// Drop queued/deferred visual relight debt for a column being evicted.
  /// Callers must invalidate its disk-light-complete flag when this abandons
  /// unfinished lighting so a later disk load recomputes it.
  int CancelTerrainColumnRelight(glm::ivec2 world_block_key);
  TerrainColumnRelightQueueInfo
  GetTerrainColumnRelightQueueInfo(glm::ivec2 world_block_key) const;
  int GetPendingPlayerRelightCount() const;
  /// Drop farthest far-FIFO columns until size <= soft_cap (priority
  /// untouched).
  int TrimFarRelightFifoFarthest(glm::ivec3 focus_ground, int soft_cap,
                                 int protect_horiz = -1);
  /// P1: miss / PromoteRelightHold column that far overflow must not pop.
  void SetRelightFifoPin(glm::ivec2 chunk_xz, bool valid);
  int TakeRelightFifoOverflowDropped();
  int TakeRelightFifoPinSaved();
  int TakeRelightFifoPriorityInsert();
  int TakeRelightFifoProtectBlock();
  void ClearPendingRelights(const char *reason = nullptr);
  static constexpr size_t kMaxPendingTerrainColumnLoads = 32;
  TerrainColumnRead RequestAsyncTerrainColumnLoad(UWorld &world, glm::ivec3 ground_coord,
                                                 uint64_t demand_id = 0);
  bool RequestAsyncTerrainColumnSave(UWorld &world, glm::ivec3 ground_coord);
  bool TrySaveTerrainColumnForUnload(UWorld &world, glm::ivec3 ground_coord);
  // Unload cancels workers before serializing the still-resident column.
  // Retain its data until that save/eviction has completed.
  void CancelAsyncTerrainColumnLoad(UWorld &world, glm::ivec3 ground_coord,
                                    bool retain_for_column_save = false);
  int CancelAsyncTerrainColumnLoadsOutsideRadius(UWorld &world,
                                                 glm::ivec3 center,
                                                 int radius_chunks);
  bool IsTerrainColumnDiskLoadPending(glm::ivec3 ground_coord) const;
  /// Indexed owner checks only; no FIFO position scans.
  bool HasTerrainColumnMetadataOwner(glm::ivec3 ground_coord) const;

  int LoadTerrainColumn(glm::ivec3 coord, UBlockWorld &block_world,
                        UBlockRegistry &registry, int max_height,
                        int min_height);
  bool SaveTerrainColumn(glm::ivec3 ground_coord, UBlockWorld &block_world,
                         UBlockRegistry &registry, int max_height,
                         int min_height);
  /// Remove all disk slices for a ground column (used when incomplete in RAM
  /// must not leave a stale complete ocean/land file behind).
  void RemoveTerrainColumnFromDisk(glm::ivec3 ground_coord, int max_height,
                                   int min_height);
  /// Preserve an incompletely read saved column until its data can be read.
  void PreserveIncompleteTerrainColumn(UBlockWorld &block_world,
                                       glm::ivec3 ground_coord);
  void LoadInitialTerrainColumns(UWorld &world, glm::vec3 spawn_point,
                                 int render_distance_chunks);

  /// Minetest-style lighting_complete: disk lightmap trusted for this column.
  bool IsColumnLightComplete(glm::ivec2 ground_xz) const;
  void SetColumnLightComplete(glm::ivec2 ground_xz, bool complete,
                              const char *reason = nullptr);
  void ClearColumnLightComplete(glm::ivec2 ground_xz,
                                const char *reason = nullptr);
  void LoadColumnLightFlags();
  void SaveColumnLightFlagsIfDirty(double *snapshot_ms = nullptr,
                                   double *enqueue_ms = nullptr,
                                   std::size_t *complete_columns_n = nullptr,
                                   uint64_t *saved_revision = nullptr);

private:
  auto MakeStoredUWorldPersistence0();

  friend void RunTerrainSaveFailureProbe(UCore &core);
  DiskAirCancellationProbe AirCancellationProbe;
  DiskReadFailureProbe ReadFailureProbe;
  std::optional<glm::ivec3> ReadFailureEditCell;
  std::optional<uint64_t> ReadFailureRecoverySaveDemand;
  bool ReadFailureRecoverySaveRequested{false};
  void ObserveReadFailureSaveProbe(UWorld &world, glm::ivec3 ground,
                                   uint64_t demand_id);
  void TraceDeniedRelightAdmission(glm::ivec2 ground_xz, int min_y, int max_y,
                                   UWorld *world_context, const char *producer,
                                   int fifo_n, int horiz, glm::ivec2 key,
                                   bool priority);
  struct TerrainReadOrder
  {
    int focus{0}, min_cy{0}, max_cy{-1}, below{-1}, above{0};
    std::optional<int> sea;
    bool focus_pending{false}, sea_pending{false};
    std::optional<int> TakeNext();
  };
  struct PendingAsyncColumnLoadState
  {
    uint64_t demand_id{0};
    int remaining_results{0};
    int highest_cy_on_disk{kNoChunkSliceOnDisk};
    TerrainReadOrder read_order;
    ChunkGenerationToken token;
    bool include_light_receipts{false};
    std::shared_ptr<std::atomic<bool>> cancellation;
    TerrainSliceLoadOwnership installed_terrain;
    std::unordered_set<glm::ivec3, IVec3Hash> consumed_slices;
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

  void BeginTerrainColumnRead(UWorld &world, glm::ivec3 ground,
                              PendingAsyncColumnLoadState &state, bool include_light);
  void SubmitTerrainColumnReads(UWorld &world, glm::ivec3 ground,
                                PendingAsyncColumnLoadState &state);
  void PumpTerrainColumnReads(UWorld &world);

  std::size_t RetireCancelledAirLoad(UWorld &world, glm::ivec3 ground_coord,
                                     PendingAsyncColumnLoadState &state);

  void FinalizeAsyncTerrainColumnLoad(UWorld &world, glm::ivec3 ground_coord,
                                      PendingAsyncColumnLoadState state);
  void EnqueueTerrainColumnRelightImpl(int world_x, int world_z, bool priority,
                                       int min_y, int max_y,
                                       bool visible_admission,
                                       const char *producer = nullptr,
                                       UWorld *world_context = nullptr);
  void PreserveRelightFifoVictimAsDeferred(glm::ivec2 world_block_key,
                                           bool priority,
                                           const char *reason = nullptr);
  bool PrioritizeNearestTerrainColumnRelight(UWorld &world,
                                             glm::ivec3 focus_ground,
                                             int radius_chunks, int scan_cap);
  void ProcessColumnLightFlagSaveResults(
      double *queue_mutex_wait_ms = nullptr,
      double *queue_mutex_held_ms = nullptr,
      std::size_t *result_count = nullptr,
      AsyncChunkIoTickMetrics *tick_metrics = nullptr);
  bool FlushColumnLightFlagsForWorldSwitch();

  // Storage outlives AsyncChunkIo's worker pool: warmup jobs retain its
  // reference until the pool joins during destruction.
  std::unique_ptr<UChunkStorageService> ChunkStorage;
  std::unique_ptr<UAsyncChunkIO> AsyncChunkIo;
  bool DiskIndexWarmupPending{false};
  std::unordered_map<glm::ivec3, PendingAsyncColumnLoadState, IVec3Hash>
      PendingAsyncColumnLoadSlices;
  static constexpr size_t kMaxTerrainReadOwners = 256;
  std::unordered_map<glm::ivec3, std::chrono::steady_clock::time_point,
                     IVec3Hash>
      FailedColumnRetryAfter;
  void ExpireTerrainReadCooldowns(std::chrono::steady_clock::time_point now);
  bool PreferFieldInputLoadNext_{true};
  std::unordered_map<glm::ivec3, ColumnSaveCompletion, IVec3Hash> PendingAsyncColumnSaveSlices;
  struct UnloadSaveReceipt
  {
    TerrainColumnSaveStamp stamp;
    std::shared_ptr<const BlockDefinitionCatalog> catalog;
    std::string folder;
    int minY{0}, maxY{0};
    uint64_t batch{0};
    bool complete{false}, success{false};
    std::chrono::steady_clock::time_point retryAfter{};
  };
  std::unordered_map<glm::ivec3, UnloadSaveReceipt, IVec3Hash> UnloadSaveReceipts;
  struct PlayerRelightRequest
  {
    std::vector<glm::ivec3> block_positions;
    int min_world_y{0};
  };
  std::deque<PlayerRelightRequest> PendingPlayerRelights;
  std::deque<glm::ivec2> PendingTerrainColumnRelights;
  std::deque<glm::ivec2> PendingTerrainColumnRelightsPriority;
  std::unordered_set<glm::ivec2, IVec2Hash> PendingTerrainColumnRelightKeys;
  /// Optional Y band per pending column (min,max); missing => full
  /// 0..MaxHeight.
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
  /// A queued boundary dependency must survive no-op admission checks and the
  /// surface-band clamp until its exact Y band has been captured.
  std::unordered_set<glm::ivec2, IVec2Hash>
      PendingBoundaryDependencyRelightKeys;
  /// FZ2.3-O2: last StreamingFrameEpoch when finalize_gate Capture submitted.
  std::unordered_map<glm::ivec2, uint64_t, IVec2Hash> RelightLastFinalizeEpoch_;
  struct DeferredFarRelightEntry
  {
    glm::ivec2 y_band;
    bool priority{false};
    std::chrono::steady_clock::time_point first_deferred_at{};
  };
  std::unordered_map<glm::ivec2, DeferredFarRelightEntry, IVec2Hash>
      DeferredFarRelightColumns;
  std::chrono::steady_clock::time_point LastDeferredFarAgeServiceAttemptAt{};
  glm::ivec2 LastDeferredFarAgeServiceWorldKey{};
  bool LastDeferredFarAgeServiceWorldKeyValid{false};
  /// Columns whose disk lightmap is trusted (lighting_complete).
  void ResetLightCompleteColumns();
  std::unordered_set<glm::ivec2, IVec2Hash> LightCompleteColumns;
  SectionLightRestoreQueue PendingSectionLightRestores;
  bool LightCompleteDirty{false};
  bool LightCompleteLoaded{false};
  UWorld *WorldContext{nullptr};
  const std::string &CurrentLightCacheSignature() const;
  bool EnsureLightCacheSignatureCurrent();
  std::string LightCompleteSignature;
  mutable std::shared_ptr<const BlockDefinitionCatalog> LightSignatureCatalog;
  mutable int LightSignatureMinY{0};
  mutable int LightSignatureMaxY{-1};
  mutable int LightSignatureMode{-1};
  mutable std::string CachedExpectedLightSignature;
  uint64_t LightCompleteRevision{0};
  bool LightCompleteSaveInFlight{false};
  std::string LightCompleteSaveWorldFolder;
  uint64_t LightCompleteSaveRevision{0};
  unsigned int LightCompleteSaveFailures{0};
  std::chrono::steady_clock::time_point LightCompleteSaveRetryAt{};
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
