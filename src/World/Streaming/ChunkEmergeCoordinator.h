#ifndef CHUNKEMERGECOORDINATOR_H
#define CHUNKEMERGECOORDINATOR_H

#include "World/Streaming/StreamingPressure.h"

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
};

} // namespace cutum

#endif
