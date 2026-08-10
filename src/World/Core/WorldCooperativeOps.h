#pragma once

#include "Core/Progress/IUProgressSink.h"
#include <chrono>
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

  ~UWorldCooperativeSession();

  void BeginLoad(UWorld &world, const std::string &world_folder_path);
  /// When resume_streaming_after_save is false (shutdown save), SaveMetadata
  /// must not recreate streamer/worker pools before PrepareForShutdown.
  void BeginSave(UWorld &world, const std::string &world_folder_path,
                 bool resume_streaming_after_save = true);
  void BeginCreate(UWorld &world, const std::string &world_name);
  /// @return true when the operation finished successfully.
  bool Tick(UWorld &world, IUProgressSink &sink, int chunkBudget);
  /// Era31 I-T4: force-complete EnterGame load when visual cap reached.
  bool ForceCapEnterGameVisual(UWorld &world, IUProgressSink &sink);
  void Cancel();
  void CancelBackgroundWorkers();

  /// When true, main-thread TickAsyncChunkSystems should not run (coop owns or quiesces workers).
  bool BlocksStreamingTick() const;

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
    SaveMetadata,
    GenerateColumns,
    PostCreate,
    Done
  };

  void Report(IUProgressSink &sink, const std::string &phaseId, float fraction,
              const std::string &message) const;
  void ScanChunkFiles(UWorld &world);
  void ScanSaveChunkCoords(UWorld &world);
  void InitGenerationGrid(UWorld &world, bool center_on_load_focus = false);
  void SealFluidInGenerationPatch(UWorld &world);
  bool LoadOneChunkFile(UWorld &world, const std::filesystem::path &path);
  bool AdvanceGeneration(UWorld &world, int budget);

  Phase CurrentPhase{Phase::Init};
  std::string FolderPath;
  std::string TargetWorldName;

  std::vector<std::filesystem::path> ChunkFiles;
  size_t ChunkFileIndex{0};
  std::vector<glm::ivec3> SaveChunkCoords;
  size_t SaveChunkIndex{0};
  bool SaveUsesTerrainColumns{false};
  int SaveDrainIoFrames{0};

  bool SpatialStreamingLoad{false};
  std::string ChunksFileName;

  int SpatialRadius{0};
  glm::ivec3 SpatialCenter{0};
  int SpatialDx{0};
  int SpatialDz{0};
  int MeshWarmupTicks{0};
  std::chrono::steady_clock::time_point MeshWarmupStartedAt{};
  std::chrono::steady_clock::time_point RelightColumnsStartedAt{};
  size_t MeshWarmupStartPending{0};
  bool MeshWarmupFinalizeOnly{false};
  bool ProceduralFillLoadPath{false};
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
  size_t EmissiveChunkRelightIndex{0};
  size_t MeshWarmupProcessedMax{0};
  size_t MeshWarmupCompletedTotal{0};
  int StreamingWarmupTicks{0};

  void BeginDeferredRelightQueue(UWorld &world);
  void BeginBulkChunkRelightQueue(UWorld &world);
  void BeginColumnRelightQueue(UWorld &world);
  void BeginEmissiveBlockLightQueue(UWorld &world);
  void FinishEmissiveBlockLightRelight(UWorld &world);
  void BeginMeshWarmupInner(UWorld &world);
  void BeginMeshWarmup(UWorld &world);
  void ReportMeshWarmupStart(IUProgressSink &sink) const;
  void BeginPrepareEnter();
  const char *PhaseId() const;
  int GenCenterX{0};
  int GenCenterZ{0};
  int GenPatchMinX{0};
  int GenPatchMaxX{0};
  int GenPatchMinZ{0};
  int GenPatchMaxZ{0};
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
  std::vector<GenChunkEntry> GenChunkQueue;
  size_t GenChunkScheduleIndex{0};
  void EnsureParallelGenerationInfrastructure(UWorld &world);
  bool AdvanceParallelGeneration(UWorld &world, int budget);
  std::vector<GenColumnEntry> GenColumnQueue;
  size_t GenColumnIndex{0};
  int GenTotalColumns{0};
  int GenDoneColumns{0};

  int ChunkFilesRead{0};
  size_t VoxelsFromChunkFiles{0};
  bool NeedsProceduralFill{false};
  bool Failed{false};
  std::string ErrorMessage;
  Phase LastDiagPhase{Phase::Done};
  CooperativeParallelGenState *ParallelGen{nullptr};
};

} // namespace cutum
