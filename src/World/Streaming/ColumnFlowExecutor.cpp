#include "World/Streaming/ColumnFlowExecutor.h"

#include "World/Core/World.h"
#include "World/Streaming/ColumnRenderablePolicy.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace cutum
{

namespace
{
UColumnFlowExecutor gExecutor;
} // namespace

UColumnFlowExecutor &GetColumnFlowExecutor()
{
  return gExecutor;
}

void UColumnFlowExecutor::RequestPromoteRelight(glm::ivec2 near_column,
                                                int priority)
{
  Enqueue(near_column, ColumnWorkKind::PromoteRelight, priority);
}

void UColumnFlowExecutor::DrainRemeshSeamBudget(UWorld &world, int max_columns)
{
  // One Dispatch(RemeshSeam) == SyncIdle(1); budget N needs a single SyncIdle(N)
  // because Enqueue dedupes (column,kind) and cannot queue N identical seams.
  if (max_columns > 0)
  {
    world.SyncIdleFocusGreedyRemesh(max_columns);
  }
}

void UColumnFlowExecutor::Dispatch(UWorld &world, const ColumnWorkItem &work,
                                   glm::ivec3 focus_ground_horiz,
                                   int focus_radius, int admit_batch)
{
  const glm::ivec2 focus_xz(focus_ground_horiz.x, focus_ground_horiz.z);
  // Sentinel focus enqueue → full ring scan (nullptr). Per-column → filter.
  const glm::ivec2 *only =
      (work.column.x == focus_xz.x && work.column.y == focus_xz.y) ? nullptr
                                                                  : &work.column;
  switch (work.kind)
  {
  case ColumnWorkKind::FirstMesh:
    world.AdmitFocusVisibleMissing(admit_batch, glm::vec2(0.0f), only);
    world.AdmitFocusMeshIngress(1);
    break;
  case ColumnWorkKind::RelightThenMesh:
    world.RecoverUnlitFocusMeshes(1, only);
    break;
  case ColumnWorkKind::RemeshSeam:
    world.SyncIdleFocusGreedyRemesh(1);
    break;
  case ColumnWorkKind::PromoteRelight:
    world.PromotePendingLightRelightsNear(focus_ground_horiz, focus_radius);
    break;
  }
}

int UColumnFlowExecutor::DrainBudget(UWorld &world, int n,
                                     glm::ivec3 focus_ground_horiz,
                                     int focus_radius, int admit_batch)
{
  int drained = 0;
  ColumnWorkItem work{};
  while (drained < n && scheduler_.DrainOne(work))
  {
    Dispatch(world, work, focus_ground_horiz, focus_radius, admit_batch);
    ++drained;
  }
  return drained;
}

void UColumnFlowExecutor::TickDerived(UWorld &world,
                                      glm::ivec3 focus_ground_horiz,
                                      int focus_radius, bool moving,
                                      bool missing_visible_mesh,
                                      bool visual_holes, bool idle_remesh_debt,
                                      bool idle_focus_dirty_debt,
                                      int pending_focus_n, int recover_n,
                                      int admit_n)
{
  const glm::ivec2 focus(focus_ground_horiz.x, focus_ground_horiz.z);
  std::vector<glm::ivec2> pending_cols;
  std::vector<glm::ivec2> sticky_cols;
  world.CollectPendingLightFocusColumns(focus_ground_horiz, focus_radius,
                                        pending_cols, std::max(4, recover_n));
  world.CollectStickyRemeshFocusColumns(focus_ground_horiz, focus_radius,
                                        sticky_cols, std::max(2, recover_n / 2));

  // should_relight_then_mesh / should_promote_relight: real pending columns.
  if (!idle_remesh_debt && !idle_focus_dirty_debt && recover_n > 0)
  {
    int enq = 0;
    for (const glm::ivec2 &col : pending_cols)
    {
      if (enq >= recover_n)
      {
        break;
      }
      Enqueue(col, ColumnWorkKind::RelightThenMesh, recover_n + 50 - enq);
      Enqueue(col, ColumnWorkKind::PromoteRelight, recover_n + 20 - enq);
      ++enq;
    }
    if (enq == 0)
    {
      Enqueue(focus, ColumnWorkKind::RelightThenMesh, recover_n);
    }
  }

  // should_remesh_seam: sticky remesh debt + stale-dark (TD-ARCH-026).
  // Near ring (horiz≤2): always RelightThenMesh + RemeshSeam — edge black faces
  // (manual_1940 dark_face_near≈1000+) need light progress, not remesh alone.
  std::vector<glm::ivec2> stale_dark_cols;
  const int stale_cap =
      std::max(4, recover_n); // prefer draining near stale over tiny batches
  world.CollectStaleDarkFocusColumns(focus_ground_horiz, focus_radius,
                                     stale_dark_cols, stale_cap);
  EnqueueStickyStaleRepairTickets(scheduler_, focus, sticky_cols,
                                  stale_dark_cols);
  for (const glm::ivec2 &col : stale_dark_cols)
  {
    world.NoteColumnRepairNeeded(col);
  }

  // should_first_mesh: focus sentinel when holes / missing (Admit filters).
  if (admit_n > 0 &&
      ((!moving && (missing_visible_mesh || pending_focus_n > 0) &&
        !idle_remesh_debt) ||
       (moving && (visual_holes || missing_visible_mesh))))
  {
    Enqueue(focus, ColumnWorkKind::FirstMesh, admit_n + 50);
  }
}

void UColumnFlowExecutor::DrainIdlePendingLight(
    UWorld &world, glm::ivec3 focus_ground_horiz, int focus_radius, int budget,
    bool allow_sync, double /*last_frame_ms*/, int /*pending_focus_count*/,
    bool /*missing_visible_mesh*/)
{
  world.DrainIdleFocusPendingLight(focus_ground_horiz, focus_radius, budget);
  if (allow_sync)
  {
    world.DrainIdleFocusPendingLightSync(focus_ground_horiz, focus_radius, 1);
  }
}

} // namespace cutum
