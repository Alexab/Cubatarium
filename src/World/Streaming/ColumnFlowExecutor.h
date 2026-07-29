#pragma once

#include "World/Streaming/ColumnFlowScheduler.h"

#include <glm/glm.hpp>

namespace cutum
{

class UWorld;

/// V4 single owner for focus column Admit / Recover / Promote / idle pending.
/// ChunkEmergeCoordinator must not call World Admit/Recover/Promote directly —
/// route through this executor (derived should_* + DrainBudget).
class UColumnFlowExecutor
{
public:
  UColumnFlowScheduler &Scheduler() { return scheduler_; }
  const UColumnFlowScheduler &Scheduler() const { return scheduler_; }

  void Enqueue(glm::ivec2 column, ColumnWorkKind kind, int priority)
  {
    scheduler_.Enqueue(column, kind, priority);
  }

  void Clear() { scheduler_.Clear(); }

  /// Scan focus truth and enqueue derived work (uses real column coords).
  void TickDerived(UWorld &world, glm::ivec3 focus_ground_horiz, int focus_radius,
                   bool moving, bool missing_visible_mesh, bool visual_holes,
                   bool idle_remesh_debt, bool idle_focus_dirty_debt,
                   int pending_focus_n, int recover_n, int admit_n);

  /// Drain up to n queued items; dispatch uses item.column.
  int DrainBudget(UWorld &world, int n, glm::ivec3 focus_ground_horiz,
                  int focus_radius, int admit_batch = 1);

  /// Idle PendingLight FIFO progress (single owner for DrainIdle*).
  void DrainIdlePendingLight(UWorld &world, glm::ivec3 focus_ground_horiz,
                             int focus_radius, int budget, bool allow_sync,
                             double last_frame_ms, int pending_focus_count,
                             bool missing_visible_mesh);

  /// Promote path: enqueue PromoteRelight then drain (no direct World promote
  /// from emerge except via Dispatch).
  void RequestPromoteRelight(glm::ivec2 near_column, int priority);

  /// Synchronous ColumnFlow promote (Dispatch only — does not steal DrainBudget
  /// from FirstMesh/Remesh tickets already queued).
  void RunPromoteRelightNow(UWorld &world, glm::ivec3 focus_ground_horiz,
                            int focus_radius);

  /// True if column has a live ColumnFlow repair/admit/promote ticket queued.
  bool HasRepairTicket(glm::ivec2 column) const;

  /// Sticky/seam remesh budget (ColumnFlow-only; SyncIdle lives in Dispatch).
  void DrainRemeshSeamBudget(UWorld &world, int max_columns);

private:
  void Dispatch(UWorld &world, const ColumnWorkItem &work,
                glm::ivec3 focus_ground_horiz, int focus_radius, int admit_batch);

  UColumnFlowScheduler scheduler_;
};

UColumnFlowExecutor &GetColumnFlowExecutor();

} // namespace cutum
