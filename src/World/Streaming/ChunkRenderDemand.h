#pragma once

#include "World/Chunks/ChunkManager.h"
#include "World/Diagnostics/JobStageTrace.h"

#include <cstdint>
#include <glm/glm.hpp>
#include <unordered_map>

namespace cutum
{

/// A21 P2: shadow per-chunk demand owner. When true, NoteDemand may skip
/// redundant Dirty admits; store observes installs without replacing planners.
inline constexpr bool kChunkDemandShadow = true;

/// A21 P2.7 cutover: when true, column ClearFaceDebt is forbidden (sole writer =
/// per-chunk demand store). Default OFF — dual adapters remain until AF evidence.
/// Rollback: set false (never leave both cutover ON and column clears active).
inline bool &ChunkDemandCutoverEnabled()
{
  static bool enabled = false;
  return enabled;
}

/// True while legacy column FaceDebt clear is still allowed (shadow / pre-cutover).
inline bool ChunkDemandAllowsColumnFaceDebtClear()
{
  return !ChunkDemandCutoverEnabled();
}

enum class DemandResult : uint8_t
{
  NewDemand = 0,
  Coalesced,
  AlreadySatisfied
};

enum class InstallResult : uint8_t
{
  Published = 0,
  RetainedAwaitingSuccessor,
  RejectedRetryable,
  CancelledSuperseded
};

struct ChunkRenderDemandRecord
{
  glm::ivec3 coord{};
  uint64_t desired_geom_rev{0};
  uint64_t desired_light_rev{0};
  uint64_t desired_coverage_gen{0};
  uint64_t published_geom_rev{0};
  uint64_t published_light_rev{0};
  uint64_t active_attempt_id{0};
  JobStage active_stage{JobStage::Created};
  double last_progress_ms{0.0};
  bool has_active_attempt{false};
  bool retained_awaiting_successor{false};
  /// Optional peer coverage generation per face 0..5.
  uint64_t waiting_peer_gen[6]{};
  /// P2.4 shadow FaceDebt keyed by this chunkXYZ (column mask remains legacy).
  uint8_t face_debt_mask{0};
};

/// Per-chunk render demand store (process singleton for Cache + World callers).
class UChunkRenderDemandStore
{
public:
  static UChunkRenderDemandStore &Get();

  ChunkRenderDemandRecord *Find(glm::ivec3 coord);
  const ChunkRenderDemandRecord *Find(glm::ivec3 coord) const;
  ChunkRenderDemandRecord &GetOrCreate(glm::ivec3 coord);

  /// Raise or coalesce desire. AlreadySatisfied when published meets desired.
  DemandResult NoteDemand(glm::ivec3 coord, uint64_t desired_geom_rev,
                          uint64_t desired_light_rev,
                          uint64_t desired_coverage_gen = 0);

  void NoteStageProgress(glm::ivec3 coord, JobStage stage,
                         uint64_t attempt_id = 0, double now_ms = 0.0);

  void NoteInstallResult(glm::ivec3 coord, InstallResult result,
                         uint64_t published_geom_rev = 0,
                         uint64_t published_light_rev = 0);

  /// Face debt keyed by chunkXYZ/face; optional peer coverage generation (P2.4).
  /// peer_gen != 0 stores required peer generation on newly set faces.
  void NoteFaceDebt(glm::ivec3 chunk_xyz, uint8_t face_mask,
                    uint64_t peer_gen = 0);
  /// Clear face bits for this publisher only. When peer_gen != 0, a face clears
  /// only if waiting_peer_gen[face] is 0 or matches peer_gen.
  void NoteFaceDebtSatisfied(glm::ivec3 chunk_xyz, uint8_t face_mask,
                             uint64_t peer_gen = 0);

  struct ReconcileStats
  {
    int checked{0};
    int mismatch_desired_vs_published{0};
    int orphan_active{0};
    int retained_awaiting{0};
  };
  /// Level check desired vs published; bounded scan for maintenance.
  ReconcileStats ReconcileMaintenance(int max_n);

  uint64_t AlreadySatisfiedSkipN() const { return AlreadySatisfiedSkipN_; }
  uint64_t CoalesceN() const { return CoalesceN_; }
  uint64_t NewDemandN() const { return NewDemandN_; }
  uint64_t ShadowMismatchN() const { return ShadowMismatchN_; }
  uint64_t RetainSuccessorNoteN() const { return RetainSuccessorNoteN_; }
  void NoteShadowMismatch() { ++ShadowMismatchN_; }

  void Clear();
  size_t Size() const { return Records_.size(); }

private:
  UChunkRenderDemandStore() = default;

  std::unordered_map<glm::ivec3, ChunkRenderDemandRecord, IVec3Hash> Records_;
  uint64_t NextAttemptId_{1};
  size_t ReconcileCursor_{0};
  uint64_t AlreadySatisfiedSkipN_{0};
  uint64_t CoalesceN_{0};
  uint64_t NewDemandN_{0};
  uint64_t ShadowMismatchN_{0};
  uint64_t RetainSuccessorNoteN_{0};
};

} // namespace cutum
