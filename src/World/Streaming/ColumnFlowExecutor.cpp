#include "World/Streaming/ColumnFlowExecutor.h"
#include "World/Streaming/ColumnDesiredStage.h"
#include "World/Streaming/ColumnTicketMap.h"
#include "World/Streaming/ColumnEmergeState.h"

#include "Render/Camera/Camera.h"
#include "World/Core/World.h"
#include "World/Streaming/ColumnDesiredStage.h"
#include "World/Streaming/ColumnEmergeState.h"
#include "World/Streaming/ColumnRenderablePolicy.h"
#include "World/Streaming/SoftDeferEmptyPolicy.h"
#include "World/Streaming/OceanCruisePolicy.h"
#include "World/Streaming/OceanFrontierPolicy.h"
#include "World/Streaming/NearFovWorkPriority.h"
#include "World/Streaming/RelightFifoPolicy.h"
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

void UColumnFlowExecutor::BeginFrame()
{
  promote_pending_ = false;
  promote_enqueued_ = false;
  promote_priority_ = 0;
  promote_column_ = glm::ivec2(0);
}

void UColumnFlowExecutor::SetCaptureWitnessPin(glm::ivec2 column, bool valid,
                                               int age, bool hold)
{
  capture_pin_valid_ = valid;
  capture_pin_col_ = column;
  capture_pin_age_ = age;
  capture_pin_hold_ = hold;
}

glm::ivec2 UColumnFlowExecutor::ResolveRelightPromoteColumn(
    glm::ivec2 fallback) const
{
  if (capture_pin_valid_ && capture_pin_hold_)
  {
    return capture_pin_col_;
  }
  if (promote_hold_valid_)
  {
    return promote_hold_col_;
  }
  return fallback;
}

ColumnJobStage UColumnFlowExecutor::GetColumnJobStage(glm::ivec2 column) const
{
  const auto it = column_job_stage_.find(ColumnKey(column));
  if (it == column_job_stage_.end())
  {
    return ColumnJobStage::Absent;
  }
  return it->second;
}

void UColumnFlowExecutor::SetColumnJobStage(glm::ivec2 column,
                                            ColumnJobStage stage)
{
  column_job_stage_[ColumnKey(column)] = stage;
}

void UColumnFlowExecutor::RequestPromoteRelight(glm::ivec2 near_column,
                                                int priority)
{
  if (capture_pin_valid_ && capture_pin_hold_ && near_column != capture_pin_col_)
  {
    near_column = capture_pin_col_;
  }
  if (promote_hold_valid_ && near_column != promote_hold_col_)
  {
    return;
  }
  if (!promote_pending_)
  {
    promote_pending_ = true;
    promote_column_ = near_column;
    promote_priority_ = priority;
    return;
  }
  if (priority > promote_priority_)
  {
    promote_priority_ = priority;
    promote_column_ = near_column;
  }
}

void UColumnFlowExecutor::SetPromoteRelightHold(glm::ivec2 column, bool hold)
{
  promote_hold_valid_ = hold;
  promote_hold_col_ = column;
}

void UColumnFlowExecutor::FlushPromoteRequest()
{
  if (!promote_pending_ || promote_enqueued_)
  {
    return;
  }
  Enqueue(promote_column_, ColumnWorkKind::PromoteRelight, promote_priority_);
  promote_enqueued_ = true;
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
                                               int /*focus_radius*/)
{
  (void)world;
  RequestPromoteRelight(
      glm::ivec2(focus_ground_horiz.x, focus_ground_horiz.z), 40);
}

bool UColumnFlowExecutor::HasRepairTicket(glm::ivec2 column) const
{
  // Era17 I-H1: ticket SoT = live Flow queue membership only (not live-window).
  // Progress (Dirty/Inflight/PendingLight) is tracked separately via
  // UWorld::ColumnHasRepairProgress for NoTicket / Collect skip.
  return scheduler_.ContainsColumn(column);
}

void UColumnFlowExecutor::DrainRemeshSeamBudget(UWorld &world, int max_columns)
{
  // One AdvanceColumn(RemeshSeam) == SyncIdle(1); budget N needs a single SyncIdle(N)
  // because Enqueue dedupes (column,kind) and cannot queue N identical seams.
  if (max_columns > 0)
  {
    world.SyncIdleFocusGreedyRemesh(max_columns);
  }
}

void UColumnFlowExecutor::AdvanceColumn(UWorld &world, const ColumnWorkItem &work,
                                   glm::ivec3 focus_ground_horiz,
                                   int focus_radius, int admit_batch)
{
  last_dispatch_frame_[CooldownKey(work.column, work.kind)] = frame_counter_;
  ColumnEmergeState requested = ColumnEmergeState::Meshing;
  switch (work.kind)
  {
  case ColumnWorkKind::RelightThenMesh:
  case ColumnWorkKind::PromoteRelight:
    requested = ColumnEmergeState::Lighting;
    SetColumnJobStage(work.column, ColumnJobStage::PendingLight);
    break;
  case ColumnWorkKind::FirstMesh:
  case ColumnWorkKind::RemeshSeam:
    requested = ColumnEmergeState::Meshing;
    SetColumnJobStage(work.column, ColumnJobStage::Meshing);
    break;
  }
  const glm::ivec3 ground(work.column.x, 0, work.column.y);
  world.SetColumnEmergeState(ground, requested);
  // Closeout E0/E: TicketMap desire + one-inflight mirror on ColumnRecord.
  {
    const int horiz =
        std::max(std::abs(work.column.x - focus_ground_horiz.x),
                 std::abs(work.column.y - focus_ground_horiz.z));
    const ColumnTicketLevel ticket = TicketLevelForRing(horiz);
    const bool missing = work.kind == ColumnWorkKind::FirstMesh;
    const bool pending = work.kind == ColumnWorkKind::RelightThenMesh ||
                         work.kind == ColumnWorkKind::PromoteRelight;
    const bool dark = work.kind == ColumnWorkKind::RemeshSeam;
    const ColumnDesiredStage desired = DesiredStageFromTicket(
        ticket, world.GetColumnEmergeState(ground), missing, pending, dark);
    auto &rec = world.GetColumnRecords().GetOrCreate(work.column);
    rec.desired = desired;
    rec.inflight_job = frame_counter_ == 0 ? 1 : frame_counter_;
  }
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
  {
    const int admitted =
        world.AdmitFocusVisibleMissing(admit_batch, forward_xz, only, work.cy);
    if (admitted > 0)
    {
      world.GetPhysicsTelemetryMutable().FmDirtyEnqueueFromColumnFlowN +=
          admitted;
      world.GetPhysicsTelemetryMutable().FmDirtyEnqueueN += admitted;
    }
    break;
  }
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
    // NearTerrain: far FIFO → priority. PendingLight pass only when focus has
    // light debt (ghost / Keys repair) — skip second O(n) scan otherwise.
    world.PromoteNearTerrainColumnRelights(focus_ground_horiz, focus_radius);
    if (world.HasPendingLightBeforeMeshNear(focus_ground_horiz, focus_radius))
    {
      world.PromotePendingLightRelightsNear(focus_ground_horiz, focus_radius);
    }
    break;
  }
}

int UColumnFlowExecutor::DrainBudget(UWorld &world, int n,
                                     glm::ivec3 focus_ground_horiz,
                                     int focus_radius, int admit_batch)
{
  FlushPromoteRequest();
  ++frame_counter_;
  int drained = 0;
  ColumnWorkItem work{};
  while (drained < n && scheduler_.DrainOne(work))
  {
    AdvanceColumn(world, work, focus_ground_horiz, focus_radius, admit_batch);
    ++drained;
  }
  world.GetPhysicsTelemetryMutable().ColumnBumpDenied +=
      static_cast<int>(scheduler_.DeniedCount());
  scheduler_.ClearDeniedCount();
  world.GetPhysicsTelemetryMutable().ColumnFlowUpgradeN +=
      static_cast<int>(scheduler_.UpgradeCount());
  scheduler_.ClearUpgradeCount();
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
                                      int pending_async, bool prep_over_budget)
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
    item.priority =
        ColumnFlowFirstMeshPriority(100 + admit_n, /*horiz=*/0, focus_radius);
    item.scan_full_focus = true;
    item.cy = -1;
    Enqueue(item);
    // Phase 3: ticket → ColumnRecord.desired dual-write.
    const ColumnTicketLevel ticket = TicketLevelForRing(0);
    const ColumnDesiredStage desired = DesiredStageFromTicket(
        ticket, world.GetColumnEmergeState(glm::ivec3(focus.x, 0, focus.y)),
        true, false, false);
    world.GetColumnRecords().SetDesired(focus, desired);
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
      const int horiz =
          std::max(std::abs(col.x - focus.x), std::abs(col.y - focus.y));
      Enqueue(col, ColumnWorkKind::RelightThenMesh,
              ColumnFlowRelightPriorityUnderMiss(relight_hi - enq, horiz,
                                                 focus_radius,
                                                 missing_visible_mesh));
      ++enq;
    }
    // One Promote/frame via coalesce — AdvanceColumn promotes focus radius.
    RequestPromoteRelight(focus, promote_hi);
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
  // FP-B3 / FP-E0: ticketed VB consume — ring RelightThenMesh when VB debt high.
  const bool vb_consume =
      IsTicketedVbConsumeMode(visible_black_no_ticket_n, visible_black_n, 0,
                              moving);
  if (vb_consume)
  {
    if (!scheduler_.Contains(focus, ColumnWorkKind::RelightThenMesh))
    {
      Enqueue(focus, ColumnWorkKind::RelightThenMesh, moving ? 92 : 95);
      world.GetPhysicsTelemetryMutable().TicketedVbConsumeN++;
    }
    if (visible_black_no_ticket_n >= 10)
    {
      const glm::ivec3 fg = focus_ground_horiz;
      int ring_enq = 0;
      constexpr int kRingTopK = 3;
      for (int dz = -4; dz <= 4 && ring_enq < kRingTopK; ++dz)
      {
        for (int dx = -4; dx <= 4 && ring_enq < kRingTopK; ++dx)
        {
          if (std::max(std::abs(dx), std::abs(dz)) > 4)
          {
            continue;
          }
          const glm::ivec2 col(fg.x + dx, fg.z + dz);
          if (col == focus)
          {
            continue;
          }
          if (scheduler_.Contains(col, ColumnWorkKind::RelightThenMesh))
          {
            continue;
          }
          Enqueue(col, ColumnWorkKind::RelightThenMesh, 88 - ring_enq);
          world.GetPhysicsTelemetryMutable().TicketedVbConsumeN++;
          ++ring_enq;
        }
      }
    }
  }
  constexpr double kStaleRepairCooldownSec = 2.0;
  const auto now = std::chrono::steady_clock::now();
  const bool cooldown_ok =
      LastStaleRepairWave.time_since_epoch().count() == 0 ||
      std::chrono::duration<double>(now - LastStaleRepairWave).count() >=
          kStaleRepairCooldownSec;
  const bool allow_stale_wave_base =
      cooldown_ok && !prep_over_budget &&
      (stale_n > 40 || (dark_n > 500 && stale_n > 0));
  const bool async_ok = pending_async < 48;
  const bool pending_light =
      world.GetPhysicsTelemetry().FocusPendingDark > 0;
  // Era32 P1: lit_pending = sticky remesh-after-light only — NOT visible_black_n
  // (VB must RelightThenMesh, not DesiredStage RemeshSeam).
  const bool lit_pending =
      world.GetPhysicsTelemetry().FocusStickyRemesh > 0;
  const bool unlit_published =
      pending_light && !missing_visible_mesh &&
      (world.GetPhysicsTelemetry().FocusDarkMesh > 0 ||
       world.GetPhysicsTelemetry().DarkFaceVoidNearN > 0);
  const bool dark_drawable =
      visible_black_n > 0 ||
      world.GetPhysicsTelemetry().DarkFaceVoidNearN > 0;
  const ColumnDesiredDecision desired = DeriveColumnDesiredStage(
      missing_visible_mesh, /*stale_focus=*/allow_stale_wave_base && async_ok,
      /*void_focus=*/!missing_visible_mesh && !moving && cooldown_ok &&
          (void_n > 200 || (void_n > 40 && stale_n > 40)),
      pending_light, lit_pending, unlit_published, dark_drawable);
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
  const int repair_cap_base =
      !moving ? std::min(12, std::max(4, recover_n))
              : std::min(6, std::max(2, recover_n / 2));
  // FZ2.7-P15b (ex-P14 F4): moving VB orphan pressure — +2 RelightThenMesh budget.
  const int repair_cap =
      (moving && visible_black_no_ticket_n > 20)
          ? std::min(8, repair_cap_base + 2)
          : repair_cap_base;

  // Era17 P1: continuous heal while VisibleBlackFocusN>0 (not only NoTicket).
  // Era19 P2: void→Relight only; stale→Remesh only; PendingLight skips Remesh.
  // Era22 I-V3: no_ticket ⇒ collect on full focus_radius (nearest 1–2).
  // Era23 I-V4/I-V7: void pressure keeps void_cap≥2 even when no_ticket=0.
  if (nearest_vb_heal || nearest_vb_no_ticket || void_pressure)
  {
    const bool enter_fov_lit = world.IsEnterFovLitPassActive();
    // Era26 I-O1: under void_pressure use full focus radius (not VB clamp≤2).
    int vb_radius = VoidRelightCollectRadius(
        focus_radius, missing_visible_mesh, void_pressure,
        visible_black_no_ticket_n > 0);
    // FZ2.1-B2c: narrow enter collect ring to reduce no_ticket peak.
    if (enter_fov_lit)
    {
      vb_radius = std::min(vb_radius, 3);
    }
    const int stale_cap =
        nearest_vb_no_ticket
            ? VisibleBlackNoTicketRepairCap(visible_black_no_ticket_n,
                                            repair_cap, moving, enter_fov_lit)
            : repair_cap;
    const int void_base =
        VoidRelightCollectCap(repair_cap, void_pressure);
    const int void_cap =
        nearest_vb_no_ticket
            ? VisibleBlackNoTicketVoidCap(
                  visible_black_no_ticket_n,
                  OceanVoidRelightDrainCapMoving(void_pressure, void_base),
                  moving)
            : OceanVoidRelightDrainCapMoving(void_pressure, void_base);
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
    // FZ2-R6 / FZ2.2-C2a: second collect idle-only; C2c enter peak one-shot.
    // FZ2.7-P15b (ex-P14 F4): also one moving second pass under no_ticket orphan.
    const bool second_pass_idle =
        nearest_vb_no_ticket && async_ok && !moving &&
        visible_black_no_ticket_n > 20;
    const bool second_pass_moving =
        nearest_vb_no_ticket && async_ok && moving &&
        visible_black_no_ticket_n > 20 &&
        static_cast<int>(stale_dark_cols.size()) < stale_cap;
    const bool second_pass_enter_peak =
        enter_fov_lit && async_ok && visible_black_no_ticket_n > 40 &&
        static_cast<int>(stale_dark_cols.size()) < stale_cap;
    if ((second_pass_idle || second_pass_moving) &&
        static_cast<int>(stale_dark_cols.size()) < stale_cap)
    {
      const int remain =
          stale_cap - static_cast<int>(stale_dark_cols.size());
      // FZ2.2-O5: narrower ring on second pass (incremental vs full rescan).
      const int second_radius = std::max(1, vb_radius - 1);
      std::vector<glm::ivec2> extra;
      world.CollectStaleDarkFocusColumns(focus_ground_horiz, second_radius,
                                         extra, remain);
      if (!extra.empty())
      {
        EnqueueVisibleBlackRepairTickets(scheduler_, focus, extra);
      }
    }
    else if (second_pass_enter_peak)
    {
      // FZ2.4-P1: enter peak second pass — cap remain≤1 (no full O(n) flood).
      const int remain = std::min(
          enter_fov_lit ? 4 : 1,
          stale_cap - static_cast<int>(stale_dark_cols.size()));
      if (remain > 0)
      {
        std::vector<glm::ivec2> extra;
        world.CollectStaleDarkFocusColumns(focus_ground_horiz, vb_radius, extra,
                                           remain);
        if (!extra.empty())
        {
          EnqueueVisibleBlackRepairTickets(scheduler_, focus, extra);
        }
      }
    }
    if (!void_dark_cols.empty())
    {
      EnqueueVoidDarkRelightTickets(scheduler_, focus, void_dark_cols);
      // Era23 I-V5: Note+FIFO on void enqueue under void pressure (void_n>T /
      // miss dual-queue). VB-heal remesh tickets still Dispatch→RecoverUnlit Note.
      const bool ocean_heal_note = IsOceanHealPressure(
          missing_visible_mesh, void_n, visible_black_n);
      if (void_pressure || ocean_heal_note)
      {
        const int note_cap =
            ocean_heal_note ? OceanHealVoidRelightNoteMinPerFrame() : 2;
        int note_n = 0;
        for (const glm::ivec2 &col : void_dark_cols)
        {
          if (note_n >= note_cap)
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
    // Sticky maintenance: FirstMesh for undrawn; clear sticky if repair owned.
    // ColPipe P1: no RemeshSeam dual-feed on PreferKick drawables.
    for (const glm::ivec2 &col : sticky_cols)
    {
      if (world.IsPendingLightBeforeMesh(col))
      {
        ++world.GetPhysicsTelemetryMutable().StageSkipRemeshPendingLight;
        continue;
      }
      if (HasRepairTicket(col) || world.ColumnHasRepairProgress(col))
      {
        continue;
      }
      if (world.IsColumnDiskLightComplete(col) &&
          !world.IsColumnLitReady(glm::ivec3(col.x, 0, col.y)))
      {
        continue;
      }
      Enqueue(col, ColumnWorkKind::FirstMesh, 40);
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
      if (world.ShouldDeferRepairReticketUntilGpuApplied(col))
      {
        ++world.GetPhysicsTelemetryMutable().RepairReticketDeferredN;
        continue;
      }
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
    const UWorld::VisibleBlackFocusSample cached =
        world.GetVisibleBlackFocusSample();
    if (cached.valid)
    {
      auto &telem = world.GetPhysicsTelemetryMutable();
      telem.VisibleBlackFocusN = cached.focus_n;
      telem.VisibleBlackNoTicketN = cached.no_ticket_n;
      telem.VisibleBlackProgressN = cached.progress_n;
      telem.VisibleBlackStalledN = cached.stalled_n;
    }
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
