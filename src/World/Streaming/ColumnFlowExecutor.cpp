#include "World/Streaming/ColumnFlowExecutor.h"

#include "World/Core/World.h"
#include "World/Persistence/WorldPersistence.h"
#include "World/Streaming/ColumnRenderablePolicy.h"

#include <algorithm>
#include <chrono>
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

void UColumnFlowExecutor::RunPromoteRelightNow(UWorld &world,
                                               glm::ivec3 focus_ground_horiz,
                                               int focus_radius)
{
  ColumnWorkItem work{};
  work.column = glm::ivec2(focus_ground_horiz.x, focus_ground_horiz.z);
  work.kind = ColumnWorkKind::PromoteRelight;
  work.priority = 100;
  Dispatch(world, work, focus_ground_horiz, focus_radius, /*admit_batch=*/1);
}

bool UColumnFlowExecutor::HasRepairTicket(glm::ivec2 column) const
{
  return scheduler_.Contains(column, ColumnWorkKind::RemeshSeam) ||
         scheduler_.Contains(column, ColumnWorkKind::RelightThenMesh) ||
         scheduler_.Contains(column, ColumnWorkKind::PromoteRelight) ||
         scheduler_.Contains(column, ColumnWorkKind::FirstMesh);
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
    // Sole promote owner: terrain FIFO + PendingLight (no direct Streaming
    // Promote* calls outside ColumnFlow).
    world.PromoteNearTerrainColumnRelights(focus_ground_horiz, focus_radius);
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
                                      int /*pending_focus_n*/, int recover_n,
                                      int admit_n)
{
  const glm::ivec2 focus(focus_ground_horiz.x, focus_ground_horiz.z);
  std::vector<glm::ivec2> pending_cols;
  std::vector<glm::ivec2> sticky_cols;
  world.CollectPendingLightFocusColumns(focus_ground_horiz, focus_radius,
                                        pending_cols, std::max(4, recover_n));
  world.CollectStickyRemeshFocusColumns(focus_ground_horiz, focus_radius,
                                        sticky_cols, std::max(2, recover_n / 2));

  // should_first_mesh FIRST: priority must beat RelightThenMesh (recover+50)
  // or DrainBudget spends the frame on Capture while the rim hole stays empty
  // (manual 131234 p09–12).
  const bool hole_first_mesh =
      admit_n > 0 && (missing_visible_mesh || visual_holes) &&
      ((!moving && !idle_remesh_debt) || moving);
  if (hole_first_mesh)
  {
    Enqueue(focus, ColumnWorkKind::FirstMesh, 100 + admit_n);
  }

  // should_relight_then_mesh / should_promote_relight: real pending columns.
  // While missing FirstMesh, keep Relight priority below FirstMesh (≤99).
  if (!idle_remesh_debt && !idle_focus_dirty_debt && recover_n > 0)
  {
    const int relight_hi =
        missing_visible_mesh ? 55 : (recover_n + 50);
    const int promote_hi =
        missing_visible_mesh ? 45 : (recover_n + 20);
    int enq = 0;
    for (const glm::ivec2 &col : pending_cols)
    {
      if (enq >= recover_n)
      {
        break;
      }
      Enqueue(col, ColumnWorkKind::RelightThenMesh, relight_hi - enq);
      Enqueue(col, ColumnWorkKind::PromoteRelight, promote_hi - enq);
      ++enq;
    }
    if (enq == 0)
    {
      Enqueue(focus, ColumnWorkKind::RelightThenMesh,
              missing_visible_mesh ? 40 : recover_n);
    }
  }

  // should_remesh_seam: sticky always; stale-dark as gated waves.
  // Cooldown + cap avoid 092627 thrash. Do NOT require NearFocusHoles —
  // after Pending clears nh→0 while DarkFaceStaleNear stays high
  // (manual 182125/190350). Skip while missing so FirstMesh wins.
  // Threshold 80 (was 200): manual 190350 idle start stale≈100 + void≈610
  // never crossed 200, so residual blacks sat for ~16s with miss=nh=0.
  std::vector<glm::ivec2> stale_dark_cols;
  std::vector<glm::ivec2> void_dark_cols;
  const int stale_n = world.GetPhysicsTelemetry().DarkFaceStaleNearN;
  const int dark_n = world.GetPhysicsTelemetry().DarkFaceNearN;
  const int void_n = world.GetPhysicsTelemetry().DarkFaceVoidNearN;
  constexpr double kStaleRepairCooldownSec = 2.0;
  const auto now = std::chrono::steady_clock::now();
  const bool cooldown_ok =
      LastStaleRepairWave.time_since_epoch().count() == 0 ||
      std::chrono::duration<double>(now - LastStaleRepairWave).count() >=
          kStaleRepairCooldownSec;
  const bool allow_stale_wave =
      !missing_visible_mesh && cooldown_ok &&
      (stale_n > 80 || (dark_n > 500 && stale_n > 0));
  // Void-edge: light field 0 — Relight near-ring (manual 190350 void≫stale).
  // Idle/stop only: while moving Relight competed with FirstMesh
  // (land_south_void_cruise miss_stuck 14).
  const bool allow_void_wave =
      !missing_visible_mesh && !moving && cooldown_ok && void_n > 200;
  if (allow_stale_wave)
  {
    const int stale_radius = focus_radius;
    const int stale_cap = std::min(4, std::max(2, recover_n / 2));
    world.CollectStaleDarkFocusColumns(focus_ground_horiz, stale_radius,
                                       stale_dark_cols, stale_cap);
  }
  if (allow_void_wave)
  {
    const int void_cap = std::min(4, std::max(2, recover_n / 2));
    world.CollectFullyDarkFocusColumns(focus_ground_horiz, /*radius=*/2,
                                       void_dark_cols, void_cap);
  }
  EnqueueStickyStaleRepairTickets(scheduler_, focus, sticky_cols,
                                  stale_dark_cols);
  EnqueueVoidDarkRelightTickets(scheduler_, focus, void_dark_cols);
  if ((allow_stale_wave && !stale_dark_cols.empty()) ||
      (allow_void_wave && !void_dark_cols.empty()))
  {
    for (const glm::ivec2 &col : stale_dark_cols)
    {
      world.NoteColumnRepairNeeded(col);
    }
    for (const glm::ivec2 &col : void_dark_cols)
    {
      world.NoteColumnRepairNeeded(col);
    }
    LastStaleRepairWave = now;
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
