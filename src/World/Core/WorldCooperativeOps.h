#pragma once

#include "Core/Progress/IUProgressSink.h"
#include <chrono>
#include <climits>
#include <filesystem>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace cutum
{

class UWorld;

struct CooperativeParallelGenState;

enum class WorldCoopKind
{
  Load,
  Save,
  Create
};

class UWorldCooperativeSession
{
public:
  WorldCoopKind Kind{WorldCoopKind::Load};
  bool Active{false};
  bool ResumeStreamingAfterSave{true};
  bool PrepareCreateVisuals{true};

  ~UWorldCooperativeSession();

  void BeginLoad(UWorld &world, const std::string &world_folder_path);
  /// When resume_streaming_after_save is false (shutdown save), SaveMetadata
  /// must not recreate streamer/worker pools before PrepareForShutdown.
  void BeginSave(UWorld &world, const std::string &world_folder_path,
                 bool resume_streaming_after_save = true);
  void BeginCreate(UWorld &world, const std::string &world_name,
                   bool prepare_visuals = true);
  /// @return true when the operation finished successfully.
  bool Tick(UWorld &world, IUProgressSink &sink, int chunkBudget);
  /// Era31 I-T4: force-complete EnterGame load when visual cap reached.
  bool ForceCapEnterGameVisual(UWorld &world, IUProgressSink &sink);
  void Cancel();
  void CancelBackgroundWorkers();

  /// When true, main-thread TickAsyncChunkSystems should not run (coop owns or quiesces workers).
  bool BlocksStreamingTick() const;
  /// Load/create bar-side enter visual warmup (MeshWarmup..PrepareView).
  bool IsEnterVisualWarmupActive() const;

private:
  enum class Phase
  {
    Init,
    Metadata,
    Entities,
    ScanChunks,
    LoadChunks,
    SpatialChunks,
    RelightChunks,
    RelightColumns,
    RelightBulkChunks,
    RelightEmissiveBlockLight,
    MeshWarmup,
    PrepareEnter,
    PrepareView,
    PostLoadAnalysis,
    ProceduralFill,
    FinalizeWorld,
    ScanSaveChunks,
    DrainAsyncIo,
    SaveChunks,
    SaveMetadataMarker,
    SaveMetadataUsers,
    SaveMetadataCreatures,
    SaveMetadataWorldData,
    SaveMetadataDiag,
    GenerateColumns,
    ResolveSpawn,
    PostCreate,
    Done
  };

  void Report(IUProgressSink &sink, const std::string &phaseId, float fraction,
              const std::string &message, int subDone = -1,
              int subTotal = -1) const;
  void RecordPhaseTiming(const UWorld &world);
  /// Incremental chunk-file scan (UX U2/S8): one directory batch per call
  /// under a wall budget instead of a full-directory sweep in one tick.
  /// Returns false while more batches remain.
  bool ScanChunkFilesBatch(UWorld &world, double maxWallMs);
  void ScanChunkFiles(UWorld &world);
  void ScanSaveChunkCoords(UWorld &world);
  void InitGenerationGrid(UWorld &world, bool center_on_load_focus = false);
  void SealFluidInGenerationPatch(UWorld &world);
  /// S5: seals one row of chunks per call; returns false while more rows
  /// remain (multi-tick instead of a whole-patch sweep in one tick).
  bool SealFluidInGenerationPatchRow(UWorld &world);
  bool LoadOneChunkFile(UWorld &world, const std::filesystem::path &path);
  bool AdvanceGeneration(UWorld &world, int budget);

  // Phase handlers (UX plan R1): one method per Tick switch case;
  // switch-exit breaks became returns.
  void HandlePhaseInit(UWorld &world, IUProgressSink &sink, int budget);
  void HandlePhaseLoadChunks(UWorld &world, IUProgressSink &sink, int budget);
  void HandlePhaseSpatialChunks(UWorld &world, IUProgressSink &sink, int budget);
  void HandlePhaseRelightChunks(UWorld &world, IUProgressSink &sink, int budget);
  void HandlePhaseRelightBulkChunks(UWorld &world, IUProgressSink &sink, int budget);
  void HandlePhaseRelightColumns(UWorld &world, IUProgressSink &sink, int budget);
  void HandlePhaseRelightEmissiveBlockLight(UWorld &world, IUProgressSink &sink, int budget);
  void HandlePhaseMeshWarmup(UWorld &world, IUProgressSink &sink, int budget);
  void HandlePhasePostLoadAnalysis(UWorld &world, IUProgressSink &sink, int budget);
  void HandlePhaseProceduralFill(UWorld &world, IUProgressSink &sink, int budget);
  void HandlePhaseResolveSpawn(UWorld &world, IUProgressSink &sink, int budget);
  void HandlePhasePrepareView(UWorld &world, IUProgressSink &sink, int budget);
  // PrepareView splits by operation kind (create spawn warmup vs load
  // enter-lit warmup); the post-warmup tail stays in the dispatcher.
  void HandlePrepareViewCreate(UWorld &world, IUProgressSink &sink,
                               int budget);
  void HandlePrepareViewLoad(UWorld &world, IUProgressSink &sink, int budget);
  void HandlePhaseDrainAsyncIo(UWorld &world, IUProgressSink &sink, int budget);
  void HandlePhaseSaveChunks(UWorld &world, IUProgressSink &sink, int budget);
  void HandlePhaseGenerateColumns(UWorld &world, IUProgressSink &sink, int budget);
  void HandlePhasePostCreate(UWorld &world, IUProgressSink &sink, int budget);


  Phase CurrentPhase{Phase::Init};
  std::string TimedPhaseId;
  std::chrono::steady_clock::time_point OperationStartedAt{};
  std::chrono::steady_clock::time_point PhaseStartedAt{};
  std::string FolderPath;
  std::string TargetWorldName;

  // These options are supplied by the application runtime configuration and
  // are applied after loading legacy world metadata by Core. Preserve them
  // across LoadWorldData so the startup spatial-column phase uses the same
  // async policy as the rest of the session.
  bool RuntimeAsyncChunkGeneration{true};
  bool RuntimeAsyncChunkIo{true};
  int RuntimeMaxChunkCommitsPerFrame{3};

  std::vector<std::filesystem::path> ChunkFiles;
  size_t ChunkFileIndex{0};
  /// S8 incremental scan cursor.
  std::filesystem::path ScanChunksDir;
  bool ScanChunksActive{false};
  std::vector<std::filesystem::path> ScanChunksBatch;

  std::string ChunksFileName;

  /// Max pending seen this warmup - frozen-total display base (UX U1).
  bool ProceduralFillLoadPath{false};
  /// Era35 P3: last raw debt for monotonic display.
  /// Era35 P3: monotonic display debt (only decreases).
  /// Era44b: abort-drain after enter_mesh_abort_ms (gate stays active).
  /// Phase 5.2.0: one-shot settle_reason log for load PrepareView.
  /// A35 R0: MeshWarmup wall/tick timeout left MeshService dirty residual.
  /// LitRing C: best (lowest) FOV lit debt seen; stall abort when no progress.

  void BeginDeferredRelightQueue(UWorld &world);
  void BeginBulkChunkRelightQueue(UWorld &world);
  void BeginColumnRelightQueue(UWorld &world);
  void BeginEmissiveBlockLightQueue(UWorld &world);
  void FinishEmissiveBlockLightRelight(UWorld &world);
  void FinishHeadlessCreate(UWorld &world);
  void BeginMeshWarmupInner(UWorld &world);
  void BeginMeshWarmup(UWorld &world);
  void ReportMeshWarmupStart(IUProgressSink &sink) const;
  void BeginPrepareEnter();
  const char *PhaseId() const;
  struct GenChunkEntry
  {
    int Cx;
    int Cz;
  };
  struct GenColumnEntry
  {
    int X;
    int Z;
  };
  void EnsureParallelGenerationInfrastructure(UWorld &world);
  bool AdvanceParallelGeneration(UWorld &world, int budget);

  struct FSpatialState
  {
  int SpatialRadius{0};
  glm::ivec3 SpatialCenter{0};
  std::vector<glm::ivec3> SpatialColumnQueue;
  size_t SpatialColumnQueueIndex{0};
  std::vector<glm::ivec3> SpatialAsyncColumns;
  size_t SpatialColumnsVisited{0};
  bool SpatialStreamingLoad{false};
  };
  FSpatialState Spatial;
  struct FMeshWarmupState
  {
  int MeshWarmupTicks{0};
  std::chrono::steady_clock::time_point MeshWarmupStartedAt{};
  size_t MeshWarmupStartPending{0};
  size_t MeshWarmupSeenTotal{0};
  bool MeshWarmupFinalizeOnly{false};
  size_t MeshWarmupProcessedMax{0};
  size_t MeshWarmupCompletedTotal{0};
  bool MeshWarmupTimedOutWithDirty{false};
  };
  FMeshWarmupState MeshWarmup;
  struct FStreamingWarmupState
  {
  int StreamingWarmupTicks{0};
  int StreamingWarmupPeakDebt{0};
  int StreamingWarmupLastRawDebt{0};
  int StreamingWarmupDisplayDebt{0};
  std::chrono::steady_clock::time_point StreamingWarmupWallStart{};
  bool StreamingWarmupLitWarnLogged{false};
  bool StreamingWarmupAbortDrainMode{false};
  bool StreamingWarmupAbortLogged{false};
  bool StreamingWarmupAbortCapLogged{false};
  bool StreamingWarmupSettleLogged{false};
  int StreamingWarmupBestFovDebt{INT_MAX};
  std::chrono::steady_clock::time_point StreamingWarmupLitProgressAt{};
  bool StreamingWarmupLitStallLogged{false};
  };
  FStreamingWarmupState StreamingWarmup;
  struct FGenState
  {
  int GenCenterX{0};
  int GenCenterZ{0};
  int GenPatchMinX{0};
  int GenPatchMaxX{0};
  int GenPatchMinZ{0};
  int GenPatchMaxZ{0};
  std::vector<GenChunkEntry> GenChunkQueue;
  size_t GenChunkScheduleIndex{0};
  std::vector<GenColumnEntry> GenColumnQueue;
  size_t GenColumnIndex{0};
  int GenTotalColumns{0};
  int GenDoneColumns{0};
  /// S5 fluid-seal cursor (next chunk row X to seal).
  int FluidSealNextCx{INT_MIN};
  };
  FGenState Gen;
  struct FSaveState
  {
  std::vector<glm::ivec3> SaveChunkCoords;
  size_t SaveChunkIndex{0};
  bool SaveUsesTerrainColumns{false};
  int SaveDrainIoFrames{0};
  };
  FSaveState Save;
  struct FRelightState
  {
  std::vector<glm::ivec3> RelightQueue;
  size_t RelightQueueIndex{0};
  std::vector<glm::ivec2> ColumnRelightQueue;
  size_t ColumnRelightIndex{0};
  size_t ColumnRelightScheduledIndex{0};
  size_t ColumnRelightAppliedCount{0};
  std::vector<glm::ivec3> BulkRelightChunkQueue;
  size_t BulkRelightChunkScheduledIndex{0};
  size_t BulkRelightChunkAppliedCount{0};
  std::vector<glm::ivec3> EmissiveChunkRelightQueue;
  /// S4: chunks still needing the 16^3 emissive scan (drained per tick).
  std::vector<glm::ivec3> EmissiveChunkScanQueue;
  size_t EmissiveChunkRelightIndex{0};
  std::chrono::steady_clock::time_point RelightColumnsStartedAt{};
  };
  FRelightState Relight;
  int ChunkFilesRead{0};
  size_t VoxelsFromChunkFiles{0};
  bool NeedsProceduralFill{false};
  bool Failed{false};
  std::string ErrorMessage;
  Phase LastDiagPhase{Phase::Done};
  CooperativeParallelGenState *ParallelGen{nullptr};
};

} // namespace cutum
