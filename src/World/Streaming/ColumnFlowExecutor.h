#pragma once

#include "World/Streaming/ColumnFlowScheduler.h"
#include "World/Streaming/ColumnJobGraph.h"
#include "World/Streaming/ColumnVisualSnapshot.h"

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
    promote_pending_ = false;
    promote_enqueued_ = false;
    promote_priority_ = 0;
  }

  /// Clear per-frame Promote coalesce (call at UpdateStreaming BeginFrame).
  void BeginFrame();

  /// Scan focus truth and enqueue derived work (uses real column coords).
  /// last_frame_ms / pending_async gate stand stale-wave under miss (manual 131827).
  void TickDerived(UWorld &world, glm::ivec3 focus_ground_horiz, int focus_radius,
                   bool moving, bool missing_visible_mesh, bool visual_holes,
                   bool idle_remesh_debt, bool idle_focus_dirty_debt,
                   int pending_focus_n, int recover_n, int admit_n,
                   double last_frame_ms = 0.0, int pending_async = 0,
                   bool prep_over_budget = false);

  /// Drain up to n queued items; AdvanceColumn uses item.column.
  int DrainBudget(UWorld &world, int n, glm::ivec3 focus_ground_horiz,
                  int focus_radius, int admit_batch = 1);

  /// Idle PendingLight FIFO progress (single owner for DrainIdle*).
  void DrainIdlePendingLight(UWorld &world, glm::ivec3 focus_ground_horiz,
                             int focus_radius, int budget, bool allow_sync,
                             double last_frame_ms, int pending_focus_count,
                             bool missing_visible_mesh);

  /// Coalesce PromoteRelight to one ticket/frame (max priority). Flushed in
  /// DrainBudget — no direct World promote from emerge except via AdvanceColumn.
  void RequestPromoteRelight(glm::ivec2 near_column, int priority);

  /// True when RequestPromoteRelight ran this frame and Flush not yet done.
  bool HasPendingPromoteRequest() const
  {
    return promote_pending_ && !promote_enqueued_;
  }

  /// Enqueue PromoteRelight (DrainBudget is the exclusive bump owner).
  void RunPromoteRelightNow(UWorld &world, glm::ivec3 focus_ground_horiz,
                            int focus_radius);

  /// P1: while true, RequestPromoteRelight ignores other columns.
  void SetPromoteRelightHold(glm::ivec2 column, bool hold);
  bool HasPromoteRelightHold() const { return promote_hold_valid_; }
  glm::ivec2 GetPromoteRelightHoldColumn() const { return promote_hold_col_; }

  /// FP-A3: cruise capture witness pin — redirect PromoteRelight to pin column.
  void SetCaptureWitnessPin(glm::ivec2 column, bool valid, int age, bool hold);

  /// FP-D3: promote target under witness / miss hold (else fallback).
  glm::ivec2 ResolveRelightPromoteColumn(glm::ivec2 fallback) const;

  /// True if column has a live ColumnFlow repair/admit/promote ticket queued
  /// or is inside post-dispatch cooldown.
  bool HasRepairTicket(glm::ivec2 column) const;

  /// Sticky/seam remesh budget (ColumnFlow-only; SyncIdle lives in Dispatch).
  void DrainRemeshSeamBudget(UWorld &world, int max_columns);

  ColumnJobStage GetColumnJobStage(glm::ivec2 column) const;
  void SetColumnJobStage(glm::ivec2 column, ColumnJobStage stage);
  /// R2.7: derive stage from world/mesh truth and sync stored map.
  void SyncColumnJobStageFromWorld(UWorld &world, glm::ivec2 column);
  void SyncFocusRingColumnJobStages(UWorld &world, glm::ivec3 focus_ground,
                                    int focus_radius);

private:
  void AdvanceColumn(UWorld &world, const ColumnWorkItem &work,
                     glm::ivec3 focus_ground_horiz, int focus_radius,
                     int admit_batch);
  void FlushPromoteRequest();
  static int64_t CooldownKey(glm::ivec2 column, ColumnWorkKind kind);

  UColumnFlowScheduler scheduler_;
  /// Rate-limit stale-dark NoteColumnRepair waves (manual 092627 thrash).
  std::chrono::steady_clock::time_point LastStaleRepairWave{};
  int frame_counter_{0};
  /// column+kind → frame when last Dispatched (cooldown 3 frames).
  std::unordered_map<int64_t, int> last_dispatch_frame_;
  static constexpr int kEnqueueCooldownFrames = 3;
  /// One PromoteRelight enqueue per streaming+emerge frame (max priority).
  bool promote_pending_{false};
  bool promote_enqueued_{false};
  glm::ivec2 promote_column_{0};
  int promote_priority_{0};
  bool promote_hold_valid_{false};
  glm::ivec2 promote_hold_col_{0};
  bool capture_pin_valid_{false};
  bool capture_pin_hold_{false};
  glm::ivec2 capture_pin_col_{0};
  int capture_pin_age_{0};
  std::unordered_map<int64_t, ColumnJobStage> column_job_stage_{};

  static int64_t ColumnKey(glm::ivec2 column)
  {
    return (static_cast<int64_t>(column.x) << 32) ^
           static_cast<uint32_t>(column.y);
  }
};

UColumnFlowExecutor &GetColumnFlowExecutor();

} // namespace cutum
