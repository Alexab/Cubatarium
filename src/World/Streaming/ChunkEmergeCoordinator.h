#ifndef CHUNKEMERGECOORDINATOR_H
#define CHUNKEMERGECOORDINATOR_H

#include "World/Chunks/ChunkManager.h"
#include "World/Streaming/SoftDeferFramePolicy.h"
#include "World/Streaming/StreamingPressure.h"

#include <unordered_map>
#include <unordered_set>

namespace cutum
{

class UWorld;
struct ProceduralSettings;

/// Per-frame budgets for chunk commit and mesh rebuild during streaming emerge.
class UChunkEmergeCoordinator
{
public:
  struct FrameBudget
  {
    int MaxChunkCommits{3};
    int MaxLoadOps{4};
    int MaxMeshDrain{8};
    int MaxMeshSchedule{8};
    /// MeshWorkAdmission::Mode after TickMeshEmerge finalize (0=Normal…).
    int AdmissionMode{0};
    int DirtyAdmitBudget{8};
    int GpuApplyMax{4};
  };

  static constexpr int kDefaultMeshDrain = 12;
  static constexpr int kDefaultMeshSchedule = 12;
  static constexpr int kWarmupMeshFlush = 256;

  const FrameBudget &GetLastBudget() const { return LastBudget; }

  FrameBudget ComputeBudget(const ProceduralSettings &procedural,
                            float movement_speed, int default_load_ops,
                            double last_frame_ms = 0.0) const;

  static FrameBudget WarmupBudget(int mesh_flush = kWarmupMeshFlush);
  static FrameBudget CooperativeWarmupBudget(int coop_budget);
  static FrameBudget CreateMeshWarmupBudget(int coop_budget);

  void BeginFrame(const ProceduralSettings &procedural, float movement_speed,
                  int default_load_ops, double last_frame_ms = 0.0);

  void TickMeshEmerge(UWorld &world, const StreamingPressureCaps &pressure);

private:
  FrameBudget LastBudget{};
  int UndrawnForceCd{0};
  int StuckSmokeCd{0};
  int FocusScanCd{0};
  int RimScanSkipStreak{0};
  /// Stand witness full-column Dirty rate-limit (manual 131827 cy0–2 rim).
  int StandWitnessColumnDirtyCd{0};
  /// Stand nh≤3 sticky frames (Inflight must not reset; separate from cruise).
  int StandRimStickyFrames{0};
  int StandRimStickyCx{0};
  int StandRimStickyCy{0};
  int StandRimStickyCz{0};
  /// Era22 I-M8: consecutive frames with FOV miss (~120f ≈ 1 period ≈2s).
  int MissWitnessAgeFrames{0};
  /// Era22 F2b: once-per-period self-heal scan for long miss witnesses.
  int MissStuckSelfHealPeriod{0};
  /// Era22 F2c: once-per-period FirstMesh pin after stuck age threshold.
  int MissStuckForcePinPeriod{0};
  /// Phase C: wall EMA for adaptive emerge cap on cruise.
  double WallEmaMs{0.0};
  /// Era51 F1a: adaptive stop-phase emerge budget with decay.
  double StopIdleEmergeMs{20.0};
  /// Era24 I-E4: SoftDefer empty / Hide⇒Ticket age (frames since first seen).
  std::unordered_map<glm::ivec3, int, IVec3Hash> SoftDeferEmptyAgeFrames;
  /// Era39: sticky SoftDefer empty ownership until healed.
  std::unordered_set<glm::ivec3, IVec3Hash> SoftDeferEmptyOwned;
  /// Era39: previous-frame SoftDefer empty set (hidden-neighbor seam).
  std::unordered_set<glm::ivec3, IVec3Hash> SoftDeferEmptyPrevSeen;
  /// Era34 P1: rotate SoftDefer empty ownership when cap saturates.
  int SoftDeferEmptyScanOffset{0};
  /// Stable SoftDefer policy POD; Set*Fn installed once against this.
  SoftDeferFramePolicy SoftDeferPolicy{};
  bool SoftDeferCallbacksInstalled{false};
};

} // namespace cutum

#endif
