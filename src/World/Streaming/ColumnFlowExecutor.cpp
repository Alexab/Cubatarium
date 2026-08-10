#include "World/Streaming/ColumnFlowExecutor.h"

#include "Render/Camera/Camera.h"
#include "World/Core/World.h"
#include "World/Streaming/ColumnDesiredStage.h"
#include "World/Streaming/ColumnRenderablePolicy.h"
#include "World/Streaming/SoftDeferEmptyPolicy.h"
#include "World/Streaming/OceanCruisePolicy.h"
#include "World/Streaming/OceanFrontierPolicy.h"
#include "World/Math/GridMath.h"
#include "WorldGen/Core/ProceduralSettings.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <unordered_set>
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

void UColumnFlowExecutor::Enqueue(const ColumnWorkItem &item)
{
  const int64_t key = CooldownKey(item.column, item.kind);
  const auto it = last_dispatch_frame_.find(key);
  if (it != last_dispatch_frame_.end() &&
      frame_counter_ - it->second < kEnqueueCooldownFrames)
  {
    return;
  }
  scheduler_.Enqueue(item);
  // Era17: do NOT arm last_dispatch at Enqueue — HasRepairTicket is Contains
  // only. last_dispatch_frame_ remains enqueue cooldown after Dispatch.
}

int64_t UColumnFlowExecutor::CooldownKey(glm::ivec2 column,
                                         ColumnWorkKind kind)
{
  return (static_cast<int64_t>(column.x) << 32) |
         (static_cast<int64_t>(column.y & 0xffff) << 16) |
         static_cast<int64_t>(kind);
}

void UColumnFlowExecutor::RunPromoteRelightNow(UWorld &world,
                                               glm::ivec3 focus_ground_horiz,
                                               int focus_radius)
{
  ColumnWorkItem work{};
  work.column = glm::ivec2(focus_ground_horiz.x, focus_ground_horiz.z);
  work.kind = ColumnWorkKind::PromoteRelight;
  work.priority = 100;
  work.scan_full_focus = false;
  Dispatch(world, work, focus_ground_horiz, focus_radius, /*admit_batch=*/1);
}

bool UColumnFlowExecutor::HasRepairTicket(glm::ivec2 column) const
{
  // Era17 I-H1: ticket SoT = live Flow queue membership only (not live-window).
  // Progress (Dirty/Inflight/PendingLight) is tracked separately via
  // UWorld::ColumnHasRepairProgress for NoTicket / Collect skip.
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
  last_dispatch_frame_[CooldownKey(work.column, work.kind)] = frame_counter_;
  const glm::ivec2 *only =
      work.scan_full_focus ? nullptr : &work.column;
  glm::vec2 forward_xz = world.GetLastMovementDirXz();
  // P1: idle / below prefetch — always camera look for FirstMesh FOV fill.
  const bool idle_like =
      world.GetLastMovementSpeed() <=
      world.GetProceduralSettings().MovementPrefetchThreshold;
  if (idle_like || glm::length(forward_xz) < 0.01f)
  {
    if (const auto camera = world.GetCurrentUserCamera())
    {
      const glm::vec3 front = camera->GetFront();
      forward_xz = glm::vec2(front.x, front.z);
    }
  }
  switch (work.kind)
  {
  case ColumnWorkKind::FirstMesh:
    world.AdmitFocusVisibleMissing(admit_batch, forward_xz, only, work.cy);
    world.AdmitFocusMeshIngress(1);
    break;
  case ColumnWorkKind::RelightThenMesh:
    world.RecoverUnlitFocusMeshes(1, only);
    break;
  case ColumnWorkKind::RemeshSeam:
    if (only)
    {
      // Era16: targeted column remesh — SyncIdle scans whole sticky set and
      // stormed emerge when every VisibleBlack ticket dispatched RemeshSeam.
      // Era17: noop Remesh must not leave enqueue cooldown (re-derive next tick).
      if (world.RemeshColumnSeamTicket(*only) <= 0)
      {
        last_dispatch_frame_.erase(CooldownKey(work.column, work.kind));
      }
    }
    else
    {
      world.SyncIdleFocusGreedyRemesh(1);
    }
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
  ++frame_counter_;
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
                                      int admit_n, double /*last_frame_ms*/,
                                      int pending_async)
{
  ++frame_counter_;
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
    ColumnWorkItem item{};
    item.column = focus;
    item.kind = ColumnWorkKind::FirstMesh;
    item.priority = 100 + admit_n;
    item.scan_full_focus = true;
    item.cy = -1;
    Enqueue(item);
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

  // should_remesh_seam: sticky always; VisibleBlack Hide⇒Ticket (Era16 P1).
  // Light RemeshSeam tickets for NoTicket orphans; classic stale_n wave keeps
  // Sticky Note. DoD = VisibleBlackNoTicketN=0 without sticky/emerge storm.
  std::vector<glm::ivec2> stale_dark_cols;
  std::vector<glm::ivec2> void_dark_cols;
  const int stale_n = world.GetPhysicsTelemetry().DarkFaceStaleNearN;
  const int dark_n = world.GetPhysicsTelemetry().DarkFaceNearN;
  const int void_n = world.GetPhysicsTelemetry().DarkFaceVoidNearN;
  const int visible_black_n =
      world.GetPhysicsTelemetry().VisibleBlackFocusN;
  const int visible_black_no_ticket_n =
      world.GetPhysicsTelemetry().VisibleBlackNoTicketN;
  constexpr double kStaleRepairCooldownSec = 2.0;
  const auto now = std::chrono::steady_clock::now();
  const bool cooldown_ok =
      LastStaleRepairWave.time_since_epoch().count() == 0 ||
      std::chrono::duration<double>(now - LastStaleRepairWave).count() >=
          kStaleRepairCooldownSec;
  const bool allow_stale_wave_base =
      cooldown_ok && (stale_n > 40 || (dark_n > 500 && stale_n > 0));
  const bool async_ok = pending_async < 48;
  const bool pending_light =
      world.GetPhysicsTelemetry().FocusPendingDark > 0;
  const bool lit_pending =
      world.GetPhysicsTelemetry().FocusStickyRemesh > 0 ||
      visible_black_n > 0;
  const bool unlit_published =
      pending_light && !missing_visible_mesh &&
      (world.GetPhysicsTelemetry().FocusDarkMesh > 0 ||
       world.GetPhysicsTelemetry().DarkFaceVoidNearN > 0);
  const ColumnDesiredDecision desired = DeriveColumnDesiredStage(
      missing_visible_mesh, /*stale_focus=*/allow_stale_wave_base && async_ok,
      /*void_focus=*/!missing_visible_mesh && !moving && cooldown_ok &&
          (void_n > 200 || (void_n > 40 && stale_n > 40)),
      pending_light, lit_pending, unlit_published);
  const bool nearest_vb_heal =
      async_ok && visible_black_n > 0;
  const bool nearest_vb_no_ticket =
      ShouldEnqueueNearestVbNoTicket(visible_black_no_ticket_n > 0, async_ok);
  const bool void_pressure =
      ShouldReserveVoidRelightSlots(void_n, visible_black_n,
                                    missing_visible_mesh);
  const bool allow_stale_wave =
      desired.stage == ColumnDesiredStage::RemeshSeam ||
      world.GetPhysicsTelemetry().FocusStickyRemesh > 0 ||
      (allow_stale_wave_base && async_ok &&
       (missing_visible_mesh ? (stale_n > 40) : true));
  const bool allow_void_wave =
      desired.stage == ColumnDesiredStage::RelightOnly ||
      desired.stage == ColumnDesiredStage::RelightThenMesh ||
      (!missing_visible_mesh && !moving && cooldown_ok &&
       (void_n > 200 || (void_n > 40 && stale_n > 40)));
  const int repair_cap =
      !moving ? std::min(12, std::max(4, recover_n))
              : std::min(6, std::max(2, recover_n / 2));

  // Era17 P1: continuous heal while VisibleBlackFocusN>0 (not only NoTicket).
  // Era19 P2: void→Relight only; stale→Remesh only; PendingLight skips Remesh.
  // Era22 I-V3: no_ticket ⇒ collect on full focus_radius (nearest 1–2).
  // Era23 I-V4/I-V7: void pressure keeps void_cap≥2 even when no_ticket=0.
  if (nearest_vb_heal || nearest_vb_no_ticket || void_pressure)
  {
    // Era26 I-O1: under void_pressure use full focus radius (not VB clamp≤2).
    const int vb_radius = VoidRelightCollectRadius(
        focus_radius, missing_visible_mesh, void_pressure,
        visible_black_no_ticket_n > 0);
    const int stale_cap =
        nearest_vb_no_ticket ? std::min(2, std::max(1, repair_cap))
                             : repair_cap;
    const int void_cap = OceanVoidRelightDrainCapMoving(
        void_pressure, VoidRelightCollectCap(repair_cap, void_pressure));
    world.CollectFullyDarkFocusColumns(focus_ground_horiz, vb_radius,
                                       void_dark_cols, void_cap);
    world.CollectStaleDarkFocusColumns(focus_ground_horiz, vb_radius,
                                       stale_dark_cols, stale_cap);
    // Drop columns already in PendingLight from stale Remesh set — void columns
    // must RelightThenMesh (Era30 I-O3), not skip as stale-only.
    if (!pending_cols.empty() && !stale_dark_cols.empty())
    {
      std::unordered_set<uint64_t> pending_keys;
      pending_keys.reserve(pending_cols.size() * 2);
      for (const glm::ivec2 &c : pending_cols)
      {
        pending_keys.insert((static_cast<uint64_t>(static_cast<uint32_t>(c.x))
                             << 32) |
                            static_cast<uint32_t>(c.y));
      }
      std::unordered_set<uint64_t> void_keys;
      void_keys.reserve(void_dark_cols.size() * 2);
      for (const glm::ivec2 &c : void_dark_cols)
      {
        void_keys.insert((static_cast<uint64_t>(static_cast<uint32_t>(c.x))
                          << 32) |
                         static_cast<uint32_t>(c.y));
      }
      stale_dark_cols.erase(
          std::remove_if(stale_dark_cols.begin(), stale_dark_cols.end(),
                         [&](const glm::ivec2 &c) {
                           const uint64_t k =
                               (static_cast<uint64_t>(
                                    static_cast<uint32_t>(c.x))
                                << 32) |
                               static_cast<uint32_t>(c.y);
                           if (!pending_keys.count(k))
                           {
                             return false;
                           }
                           const bool void_col = void_keys.count(k) > 0;
                           if (ShouldSkipStaleRemeshForPendingVoid(true, void_col))
                           {
                             ++world.GetPhysicsTelemetryMutable()
                                   .StageSkipRemeshPendingLight;
                             return true;
                           }
                           return false;
                         }),
          stale_dark_cols.end());
    }
    // Void wins over stale for the same column (Relight only).
    if (!void_dark_cols.empty() && !stale_dark_cols.empty())
    {
      std::unordered_set<uint64_t> void_keys;
      void_keys.reserve(void_dark_cols.size() * 2);
      for (const glm::ivec2 &c : void_dark_cols)
      {
        void_keys.insert((static_cast<uint64_t>(static_cast<uint32_t>(c.x))
                          << 32) |
                         static_cast<uint32_t>(c.y));
      }
      stale_dark_cols.erase(
          std::remove_if(stale_dark_cols.begin(), stale_dark_cols.end(),
                         [&](const glm::ivec2 &c) {
                           const uint64_t k =
                               (static_cast<uint64_t>(
                                    static_cast<uint32_t>(c.x))
                                << 32) |
                               static_cast<uint32_t>(c.y);
                           return void_keys.count(k) > 0;
                         }),
          stale_dark_cols.end());
    }
    if (!stale_dark_cols.empty())
    {
      EnqueueVisibleBlackRepairTickets(scheduler_, focus, stale_dark_cols);
    }
    if (!void_dark_cols.empty())
    {
      EnqueueVoidDarkRelightTickets(scheduler_, focus, void_dark_cols);
      // Era23 I-V5: Note+FIFO on void enqueue under void pressure (void_n>T /
      // miss dual-queue). VB-heal remesh tickets still Dispatch→RecoverUnlit Note.
      if (void_pressure)
      {
        int note_n = 0;
        for (const glm::ivec2 &col : void_dark_cols)
        {
          if (note_n >= 2)
          {
            break;
          }
          if (world.IsPendingLightBeforeMesh(col))
          {
            continue;
          }
          world.EnqueueVoidDarkColumnRelightNote(col);
          ++note_n;
        }
      }
    }
  }

  // Classic sticky/stale wave (thresholds + Sticky Note) when not in VB heal.
  if (!nearest_vb_heal && !nearest_vb_no_ticket && !void_pressure &&
      allow_stale_wave)
  {
    stale_dark_cols.clear();
    const int stale_radius =
        missing_visible_mesh ? std::min(2, focus_radius) : focus_radius;
    const int stale_cap =
        missing_visible_mesh ? 1 : std::min(4, std::max(2, recover_n / 2));
    world.CollectStaleDarkFocusColumns(focus_ground_horiz, stale_radius,
                                       stale_dark_cols, stale_cap);
  }
  if (!nearest_vb_heal && !nearest_vb_no_ticket && !void_pressure &&
      allow_void_wave)
  {
    void_dark_cols.clear();
    const int void_cap = std::min(4, std::max(2, recover_n / 2));
    world.CollectFullyDarkFocusColumns(focus_ground_horiz, /*radius=*/2,
                                       void_dark_cols, void_cap);
  }
  if (!nearest_vb_heal)
  {
    // Sticky maintenance: RemeshSeam only. Skip columns still in PendingLight.
    for (const glm::ivec2 &col : sticky_cols)
    {
      if (world.IsPendingLightBeforeMesh(col))
      {
        ++world.GetPhysicsTelemetryMutable().StageSkipRemeshPendingLight;
        continue;
      }
      if (!HasRepairTicket(col) && !world.ColumnHasRepairProgress(col))
      {
        Enqueue(col, ColumnWorkKind::RemeshSeam, 30);
      }
    }
    if (!stale_dark_cols.empty())
    {
      EnqueueStickyStaleRepairTickets(scheduler_, focus, /*sticky*/ {},
                                      stale_dark_cols);
    }
    if (!void_dark_cols.empty())
    {
      EnqueueVoidDarkRelightTickets(scheduler_, focus, void_dark_cols);
    }
  }
  if (cooldown_ok && allow_stale_wave_base && !stale_dark_cols.empty() &&
      !nearest_vb_heal)
  {
    for (const glm::ivec2 &col : stale_dark_cols)
    {
      world.NoteColumnRepairNeeded(col);
    }
    LastStaleRepairWave = now;
    world.GetPhysicsTelemetryMutable().StaleRepairWaveN +=
        static_cast<int>(stale_dark_cols.size());
  }
  else if (cooldown_ok && allow_void_wave && !void_dark_cols.empty() &&
           !nearest_vb_heal)
  {
    LastStaleRepairWave = now;
  }
  if (nearest_vb_heal || !stale_dark_cols.empty() || !void_dark_cols.empty())
  {
    int no_ticket = 0;
    int progress_n = 0;
    int stalled_n = 0;
    const int vb = world.CountVisibleBlackFocusMeshes(
        focus_ground_horiz, focus_radius, &no_ticket, &progress_n, &stalled_n);
    auto &telem = world.GetPhysicsTelemetryMutable();
    telem.VisibleBlackFocusN = vb;
    telem.VisibleBlackNoTicketN = no_ticket;
    telem.VisibleBlackProgressN = progress_n;
    telem.VisibleBlackStalledN = stalled_n;
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
