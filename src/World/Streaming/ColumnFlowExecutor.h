#pragma once

#include "World/Streaming/ColumnFlowScheduler.h"

#include <chrono>
#include <glm/glm.hpp>
#include <unordered_map>

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
    ColumnWorkItem item{};
    item.column = column;
    item.kind = kind;
    item.priority = priority;
    Enqueue(item);
  }

  void Enqueue(const ColumnWorkItem &item);

  void Clear()
  {
    scheduler_.Clear();
    last_dispatch_frame_.clear();
  }

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

  /// True if column has a live ColumnFlow repair/admit/promote ticket queued
  /// or is inside post-dispatch cooldown.
  bool HasRepairTicket(glm::ivec2 column) const;

  /// Sticky/seam remesh budget (ColumnFlow-only; SyncIdle lives in Dispatch).
  void DrainRemeshSeamBudget(UWorld &world, int max_columns);

private:
  void Dispatch(UWorld &world, const ColumnWorkItem &work,
                glm::ivec3 focus_ground_horiz, int focus_radius, int admit_batch);
  static int64_t CooldownKey(glm::ivec2 column, ColumnWorkKind kind);

  UColumnFlowScheduler scheduler_;
  /// Rate-limit stale-dark NoteColumnRepair waves (manual 092627 thrash).
  std::chrono::steady_clock::time_point LastStaleRepairWave{};
  int frame_counter_{0};
  /// column+kind → frame when last Dispatched (cooldown 3 frames).
  std::unordered_map<int64_t, int> last_dispatch_frame_;
  static constexpr int kEnqueueCooldownFrames = 3;
};

UColumnFlowExecutor &GetColumnFlowExecutor();

} // namespace cutum
