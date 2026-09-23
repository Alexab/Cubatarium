#pragma once

#include "World/Chunks/ChunkManager.h"
#include "World/Diagnostics/JobStageTrace.h"

#include <cstdint>
#include <cstdlib>
#include <glm/glm.hpp>
#include <unordered_map>

namespace cutum
{

/// A31: demand lifecycle writers are production authority.
/// Env CUBA_DEMAND_SHADOW=1 → observe-only (writers skipped at callsites).
inline bool &ChunkDemandAuthorityEnabled()
{
  static bool enabled = []() {
    if (const char *env = std::getenv("CUBA_DEMAND_SHADOW"))
    {
      // CUBA_DEMAND_SHADOW=1 → shadow/observe only (authority OFF).
      return !(env[0] == '1' || env[0] == 't' || env[0] == 'T');
    }
    return true;
  }();
  return enabled;
}

/// Historical name: when true, NoteDemand/Install/Reconcile writers run.
/// A31 default ON (was constexpr false). Prefer ChunkDemandAuthorityEnabled().
inline bool kChunkDemandShadow() { return ChunkDemandAuthorityEnabled(); }

/// A21 P2.7 cutover: when true, column ClearFaceDebt is forbidden (sole writer =
/// per-chunk demand store). Default ON after residual R3 AF evidence path.
/// Rollback: set false or env CUBA_DEMAND_CUTOVER=0 (never leave both cutover ON
/// and column clears active).
inline bool &ChunkDemandCutoverEnabled()
{
  static bool enabled = []() {
    if (const char *env = std::getenv("CUBA_DEMAND_CUTOVER"))
    {
      return env[0] != '0' && env[0] != '\0';
    }
    return true;
  }();
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
  uint64_t world_epoch{0};
  uint64_t incarnation{0};
  uint64_t desired_geom_rev{0};
  uint64_t desired_light_rev{0};
  uint64_t desired_coverage_gen{0};
  uint64_t published_geom_rev{0};
  uint64_t published_light_rev{0};
  uint64_t published_coverage_gen{0};
  uint64_t active_attempt_id{0};
  JobStage active_stage{JobStage::Created};
  double last_progress_ms{0.0};
  double attempt_created_ms{0.0};
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
                          uint64_t desired_coverage_gen = 0,
                          double now_ms = 0.0);

  /// Returns false if attempt_id mismatches active or stage regresses.
  bool NoteStageProgress(glm::ivec3 coord, JobStage stage,
                         uint64_t attempt_id = 0, double now_ms = 0.0);

  /// Stale attempt_id (non-zero and != active) is ignored.
  bool NoteInstallResult(glm::ivec3 coord, InstallResult result,
                         uint64_t published_geom_rev = 0,
                         uint64_t published_light_rev = 0,
                         uint64_t attempt_id = 0,
                         uint64_t published_coverage_gen = 0);

  /// A26 N1: sole path to refresh published_* without InstallResult (shadow sync).
  /// Does not clear active/retain flags — use NoteInstallResult for lifecycle.
  void NotePublishedRevs(glm::ivec3 coord, uint64_t published_geom_rev,
                         uint64_t published_light_rev);

  /// Face debt keyed by chunkXYZ/face; optional peer coverage generation (P2.4).
  /// peer_gen bumps waiting_peer_gen[f] via max on all set bits (monotonic).
  void NoteFaceDebt(glm::ivec3 chunk_xyz, uint8_t face_mask,
                    uint64_t peer_gen = 0);
  /// Clear face bits. peer_gen==0 never clears a face with waiting_peer_gen!=0.
  /// Otherwise clear iff peer_gen >= waiting[f].
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
  /// Orphan Created only after grace (attempt_created_ms / last_progress).
  ReconcileStats ReconcileMaintenance(int max_n, double now_ms = 0.0);

  /// Cancel orphan active attempts: Created with no progress past grace.
  int CancelOrphanActiveAttempts(int max_n, double now_ms = 0.0);

  /// Count records where desired != published (incl. coverage/face) and not
  /// Retain-with-live-successor / active.
  int CountUnsatisfiedDemands() const;

  /// After stop: published meets desired (geom/light/coverage), no face debt,
  /// Retain has live successor attempt, no orphan, no stall without progress.
  bool StopConverged(double now_ms = 0.0) const;

  uint64_t AlreadySatisfiedSkipN() const { return AlreadySatisfiedSkipN_; }
  uint64_t CoalesceN() const { return CoalesceN_; }
  uint64_t NewDemandN() const { return NewDemandN_; }
  uint64_t ShadowMismatchN() const { return ShadowMismatchN_; }
  uint64_t RetainSuccessorNoteN() const { return RetainSuccessorNoteN_; }
  void NoteShadowMismatch() { ++ShadowMismatchN_; }

  void Clear();
  size_t Size() const { return Records_.size(); }

  /// A31: orphan grace before Created+no-progress cancels (ms).
  static constexpr double kOrphanGraceMs = 250.0;
  /// Max age of active attempt without progress before StopConverged fails.
  static constexpr double kStallFailMs = 30000.0;

private:
  UChunkRenderDemandStore() = default;

  static bool CoverageSatisfied(const ChunkRenderDemandRecord &rec,
                                uint64_t desired_coverage_gen);
  static bool PublishedMeetsDesired(const ChunkRenderDemandRecord &rec);

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
