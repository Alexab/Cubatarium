#include "World/Core/World.h"

#include "World/Chunks/Chunk.h"
#include "World/Mesh/WorldMeshService.h"
#include "World/Persistence/WorldPersistence.h"
#include "World/Streaming/AntiFlickerPolicy.h"
#include "World/Streaming/ColumnFlowExecutor.h"
#include "World/Streaming/EnterVisualWarmupPolicy.h"
#include "World/Streaming/MeshLightStalePolicy.h"
#include "World/Streaming/RelightFifoPolicy.h"
#include "World/Streaming/RelightInstallPlanner.h"
#include "World/Streaming/VisualStagePolicy.h"

#include <chrono>
#include <climits>
#include <unordered_map>
#include <unordered_set>

namespace cutum
{
namespace
{

struct YBand
{
  int min_y{INT32_MAX};
  int max_y{INT32_MIN};
};

using Clock = std::chrono::high_resolution_clock;

double ElapsedMs(Clock::time_point t0, Clock::time_point t1)
{
  return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

ColumnChunkSnapshot BuildLitApplyChunkSnapshot(
    UWorldMeshService *mesh, const UBlockWorld &block_world, glm::ivec3 coord,
    bool revision_stale_only)
{
  ColumnChunkSnapshot snap;
  snap.coord = coord;
  if (!mesh)
  {
    return snap;
  }
  UChunkMeshCache::LitApplyMeshProbe probe{};
  mesh->FillLitApplyMeshProbe(coord, probe);
  snap.has_drawable = probe.has_drawable;
  snap.has_greedy = probe.has_greedy;
  snap.is_dirty = probe.is_dirty;
  snap.raa_pending = probe.raa_pending;
  snap.gpu_pending = probe.gpu_pending;
  snap.inflight = probe.inflight;
  snap.soft_defer = probe.soft_defer;
  snap.fully_dark = probe.fully_dark;
  snap.meshed_light_rev = probe.meshed_light_rev;
  if (const UChunk *chunk = block_world.GetChunkManager().GetChunk(coord))
  {
    snap.light_field_rev = chunk->GetLightFieldRevision();
  }
  if (revision_stale_only)
  {
    snap.still_stale =
        IsMeshLightStale(snap.meshed_light_rev, snap.light_field_rev);
  }
  else if (snap.fully_dark)
  {
    snap.still_stale = IsMeshLightStaleGpu(
        probe.gpu_resident, probe.gpu_has_dark_face, snap.meshed_light_rev,
        snap.light_field_rev);
  }
  return snap;
}

} // namespace

void UWorld::ExecuteLitApplyPlan(const LitApplyPlan &plan, const glm::ivec2 &column,
                                 const glm::ivec3 &ground, bool finalize_gate)
{
  if (!MeshService)
  {
    return;
  }
  UWorldMeshService *const mesh = MeshService.get();
  for (const glm::ivec3 &coord : plan.prefer_kick_gpu)
  {
    mesh->PreferKickPendingGpuQueued(coord);
    ++PhysicsTelemetryData.MarkRelitPreferKickN;
  }
  for (const glm::ivec3 &coord : plan.request_raa)
  {
    mesh->RequestRemeshAfterApply(coord);
    ++PhysicsTelemetryData.MarkRelitRemeshAfterApplyN;
  }
  if (!plan.mark_dirty_priority.empty() || !plan.mark_dirty.empty())
  {
    const auto dirty_t0 = Clock::now();
    for (const glm::ivec3 &coord : plan.mark_dirty_priority)
    {
      mesh->MarkDirtyPriority(coord);
      ++PhysicsTelemetryData.MarkRelitScheduleN;
      ++PhysicsTelemetryData.FmDirtyEnqueueN;
      ++PhysicsTelemetryData.FmDirtyEnqueueFromMarkRelitN;
    }
    for (const glm::ivec3 &coord : plan.mark_dirty)
    {
      mesh->MarkDirty(coord);
      ++PhysicsTelemetryData.MarkRelitScheduleN;
    }
    PhysicsTelemetryData.MarkRelitMarkDirtyMs +=
        ElapsedMs(dirty_t0, Clock::now());
  }
  PhysicsTelemetryData.MarkRelitSkipAlreadyDirtyN += plan.skip_already_dirty_n;
  PhysicsTelemetryData.MarkRelitSkipInflightN += plan.skip_inflight_n;
  PhysicsTelemetryData.MarkRelitSuppressEnterSettledN +=
      plan.suppress_enter_settled_n;
  if (plan.path == ColumnInstallPath::PrimaryConsume)
  {
    ++PhysicsTelemetryData.MarkRelitPathPrimaryConsumeN;
  }
  if (plan.erase_inflight)
  {
    AsyncRelightColumnsInFlight.erase(column);
  }
  if (finalize_gate && plan.erase_pending_light)
  {
    PendingLightBeforeMesh.erase(column);
  }
  if (finalize_gate)
  {
    SetColumnEmergeState(ground, plan.fsm_after);
    if (Persistence && plan.persistence_light_complete)
    {
      Persistence->SetColumnLightComplete(column, true);
    }
  }
  if (plan.enqueue_first_mesh)
  {
    const glm::ivec2 fm_col = plan.first_mesh_column.x != 0 ||
                                      plan.first_mesh_column.y != 0
                                  ? plan.first_mesh_column
                                  : column;
    GetColumnFlowExecutor().Enqueue(fm_col, ColumnWorkKind::FirstMesh,
                                    /*priority=*/70);
    ++PhysicsTelemetryData.MarkRelitEnqueueFirstMeshN;
  }
}

void UWorld::MarkRelitChunksForMesh(const std::vector<glm::ivec3> &relit_chunks,
                                    bool priority_mesh,
                                    const std::vector<glm::ivec2> &primary_grounds,
                                    bool finalize_pending_gate,
                                    bool primary_only)
{
  ++PhysicsTelemetryData.MarkRelitInvokedN;
  const auto total_t0 = Clock::now();
  const bool enter_gate = EnterLitGateActive;
  // FZ2.7-B1e: CountEnterFovLitDebt is O(R²)×stale-probe — only needed for
  // enter quiesce latch. Cruise MarkRelit was paying ~17ms here every Apply.
  int lit_remaining = 0;
  {
    const auto setup_t0 = Clock::now();
    if (ShouldCountEnterFovLitDebtForMarkRelit(enter_gate))
    {
      lit_remaining = CountEnterFovLitDebt();
    }
    if (enter_gate && EnterLitQuiesceAllowed(enter_gate, lit_remaining))
    {
      EnterLitQuiesceLatched = true;
    }
    PhysicsTelemetryData.MarkRelitSetupMs += ElapsedMs(setup_t0, Clock::now());
  }
  const bool enter_quiesce = enter_gate && EnterLitQuiesceLatched;
  const glm::ivec3 focus_chunk =
      UChunkManager::WorldToChunk(GetPreferredLoadFocusBlock());
  const int vb_no_ticket_n = PhysicsTelemetryData.VisibleBlackNoTicketN;
  const int vb_focus_n = PhysicsTelemetryData.VisibleBlackFocusN;
  const int vb_stalled_n = PhysicsTelemetryData.VisibleBlackStalledN;
  const bool consume_mode =
      IsTicketedVbConsumeMode(vb_no_ticket_n, vb_focus_n, vb_stalled_n,
                              /*moving=*/false) ||
      ShouldConsumeUnlitTicketedVbStand(
          false, vb_focus_n, vb_no_ticket_n,
          static_cast<int>(PhysicsTelemetryData.ChunkMeshedUnlitHidden),
          PhysicsTelemetryData.PendingLightFocus);
  const bool slim_install =
      ShouldUsePrimarySlimInstallPath(primary_only, enter_gate, enter_quiesce) ||
      consume_mode ||
      ShouldUseEnterSlimInstallPath(enter_gate, enter_quiesce, primary_only);

  if (relit_chunks.empty())
  {
    if (!slim_install)
    {
      const auto empty_t0 = Clock::now();
      for (const glm::ivec2 &g : primary_grounds)
      {
        AsyncRelightColumnsInFlight.erase(g);
        if (!finalize_pending_gate)
        {
          continue;
        }
        PendingLightBeforeMesh.erase(g);
        SetColumnEmergeState(glm::ivec3(g.x, 0, g.y), ColumnEmergeState::LitReady);
        if (MeshService && !enter_quiesce)
        {
          const glm::ivec3 ground(g.x, 0, g.y);
          const int sea = ProceduralTemplate.SeaLevel;
          const int max_y = ProceduralTemplate.MaxHeight;
          const int dirty_min = std::max(0, sea - CHUNK_SIZE);
          const int dirty_max = std::min(max_y, sea + CHUNK_SIZE * 2);
          MeshService->MarkTerrainChunkMeshDirtySeamedPriority(
              ground, dirty_min, dirty_max,
              /*include_horizontal_neighbors=*/false);
        }
      }
      PhysicsTelemetryData.MarkRelitEmptyRelitMs +=
          ElapsedMs(empty_t0, Clock::now());
    }
    else
    {
      for (const glm::ivec2 &g : primary_grounds)
      {
        AsyncRelightColumnsInFlight.erase(g);
        if (finalize_pending_gate)
        {
          PendingLightBeforeMesh.erase(g);
          SetColumnEmergeState(glm::ivec3(g.x, 0, g.y),
                               ColumnEmergeState::LitReady);
        }
      }
    }
    PhysicsTelemetryData.MarkRelitTotalMs += ElapsedMs(total_t0, Clock::now());
    return;
  }

  std::unordered_set<glm::ivec2, GroundColumnHash> primary_set;
  primary_set.reserve(primary_grounds.size() * 2 + 1);
  for (const glm::ivec2 &g : primary_grounds)
  {
    primary_set.insert(g);
  }
  if (finalize_pending_gate && MeshService && !primary_grounds.empty() &&
      !slim_install)
  {
    MeshService->GetCache().SetJustRelitFirstMeshColumn(primary_grounds.front(),
                                                        true);
  }

  std::unordered_map<glm::ivec2, YBand, GroundColumnHash> bands;
  bands.reserve(relit_chunks.size());
  {
    const auto band_t0 = Clock::now();
    for (const glm::ivec3 &coord : relit_chunks)
    {
      const glm::ivec2 col(coord.x, coord.z);
      // FZ2.7-B1f: primary_only / slim — only primary columns enter bands.
      if (ShouldFilterMarkRelitBandsToPrimary(primary_only || slim_install) &&
          primary_set.count(col) == 0)
      {
        continue;
      }
      YBand &band = bands[col];
      const int chunk_base_y = coord.y * CHUNK_SIZE;
      band.min_y = std::min(band.min_y, chunk_base_y);
      band.max_y = std::max(band.max_y, chunk_base_y + CHUNK_SIZE - 1);
    }
    PhysicsTelemetryData.MarkRelitBandMs += ElapsedMs(band_t0, Clock::now());
    PhysicsTelemetryData.MarkRelitBandsN += static_cast<int>(bands.size());
  }

  const int column_max_y = ProceduralTemplate.MaxHeight;
  const int sea = ProceduralTemplate.SeaLevel;
  const bool moving =
      LastMovementSpeed > ProceduralTemplate.MovementPrefetchThreshold;

  for (auto &[key, band] : bands)
  {
    const glm::ivec3 ground(key.x, 0, key.y);
    const bool is_primary = primary_set.count(key) != 0;
    const int focus_horiz =
        std::max(std::abs(key.x - focus_chunk.x),
                 std::abs(key.y - focus_chunk.z));

    if (is_primary)
    {
      if (!finalize_pending_gate)
      {
        AsyncRelightColumnsInFlight.erase(key);
        continue;
      }

      const auto primary_t0 = Clock::now();
      LitApplyColumnInput in{};
      in.column = key;
      in.is_primary = true;
      in.finalize_gate = finalize_pending_gate;
      in.primary_only = primary_only;
      in.consume_mode = consume_mode;
      in.enter_gate = enter_gate;
      in.enter_quiesce = enter_quiesce;
      in.suppress_relight_seam = SuppressRelightSeamDirty;
      in.priority_mesh = priority_mesh;
      in.moving = moving;
      in.focus_horiz = focus_horiz;
      in.lit_band.min_y = band.min_y;
      in.lit_band.max_y = band.max_y;
      {
        const auto flow_t0 = Clock::now();
        in.has_fm_ticket = GetColumnFlowExecutor().Scheduler().Contains(
            key, ColumnWorkKind::FirstMesh);
        in.has_repair_ticket = GetColumnFlowExecutor().HasRepairTicket(key);
        PhysicsTelemetryData.MarkRelitFlowQueryMs +=
            ElapsedMs(flow_t0, Clock::now());
      }

      const bool revision_stale = slim_install && primary_only;
      in.relit_chunks.reserve(4);
      const auto snap_t0 = Clock::now();
      for (const glm::ivec3 &coord : relit_chunks)
      {
        if (coord.x != key.x || coord.z != key.y)
        {
          continue;
        }
        ColumnChunkSnapshot snap = BuildLitApplyChunkSnapshot(
            MeshService.get(), BlockWorld, coord, revision_stale);
        in.relit_chunks.push_back(snap);
        if (snap.has_drawable)
        {
          in.any_drawable = true;
          in.column_has_drawable = true;
        }
        if (snap.has_greedy || snap.soft_defer)
        {
          in.had_mesh = true;
        }
        if (!snap.has_drawable && (snap.has_greedy || snap.soft_defer) &&
            (in.has_fm_ticket || snap.inflight || snap.gpu_pending))
        {
          in.soft_defer_empty_owned = true;
        }
      }
      PhysicsTelemetryData.MarkRelitSnapshotMs +=
          ElapsedMs(snap_t0, Clock::now());
      const bool damp_cruise_ingress =
          PhysicsTelemetryData.EditImmediateN <= 0 &&
          ShouldDampCruiseIngressSeamRemesh(
              moving, PhysicsTelemetryData.VisualHoles > 0,
              PhysicsTelemetryData.MissHoriz);
      const bool damp_stand_vb = ShouldDampMarkRelitRemeshOnStandVbDebt(
          moving, PhysicsTelemetryData.VisibleBlackNoTicketN,
          PhysicsTelemetryData.VisibleBlackFocusN);
      static int stand_seam_relit_frames = 0;
      if (!moving && PhysicsTelemetryData.MarkRelitInvokedN > 0)
      {
        ++stand_seam_relit_frames;
      }
      else
      {
        stand_seam_relit_frames = 0;
      }
      const bool damp_stand_seam_burst = !moving && stand_seam_relit_frames > 4;
      in.damp_soft_empty_remesh = ShouldDampMarkRelitRemeshOnSoftDeferEmpty(
          in.soft_defer_empty_owned, in.any_drawable,
          damp_cruise_ingress || damp_stand_vb || damp_stand_seam_burst);
      bool any_fully_dark = false;
      bool any_still_stale = false;
      for (const ColumnChunkSnapshot &snap : in.relit_chunks)
      {
        if (snap.fully_dark)
        {
          any_fully_dark = true;
        }
        if (snap.still_stale)
        {
          any_still_stale = true;
        }
      }
      in.force_stale_ticket = ShouldForceMarkRelitForTicketedStale(
          consume_mode, in.has_repair_ticket, any_fully_dark, any_still_stale,
          focus_horiz);

      const auto plan_t0 = Clock::now();
      LitApplyPlan plan = PlanColumnInstall(in);
      PhysicsTelemetryData.MarkRelitPlanMs += ElapsedMs(plan_t0, Clock::now());
      const bool focus_no_mesh_debt =
          PhysicsTelemetryData.ColumnLoadedNoMeshN > 0 ||
          PhysicsTelemetryData.UnfinishedVisual > 0;
      const bool visual_holes = PhysicsTelemetryData.UnfinishedVisual > 0;
      const bool mark_missing_once = ShouldMarkMissingOnceOnLitReady(
          finalize_pending_gate, slim_install || consume_mode,
          plan.schedule_n, focus_horiz, focus_no_mesh_debt);
      const bool mark_missing_cruise =
          !mark_missing_once &&
          ShouldMarkMissingOnCruiseMovingHoles(moving, visual_holes,
                                               focus_horiz, 4) &&
          focus_no_mesh_debt && finalize_pending_gate && plan.schedule_n == 0;
      if (mark_missing_once || mark_missing_cruise)
      {
        const int dirty_min = std::max(0, sea - CHUNK_SIZE);
        const int dirty_max =
            std::min(column_max_y, sea + CHUNK_SIZE * 2);
        const int marked = MeshService->MarkMissingSlicesDirtyPriority(
            BlockWorld, ground, dirty_min, dirty_max);
        PhysicsTelemetryData.MarkMissingPrimaryN += marked;
        PhysicsTelemetryData.MarkRelitScheduleN += marked;
        PhysicsTelemetryData.FmDirtyEnqueueN += marked;
        PhysicsTelemetryData.FmDirtyEnqueueFromMarkRelitN += marked;
        if (marked > 0)
        {
          plan.schedule_n += marked;
          plan.fsm_after = ColumnEmergeState::Meshing;
          if (!in.has_fm_ticket)
          {
            plan.enqueue_first_mesh = true;
            plan.first_mesh_column = key;
          }
        }
        else if (plan.schedule_n == 0 && !in.has_fm_ticket &&
                 !in.column_has_drawable)
        {
          // Keep PendingLight gate until FirstMesh ticket exists.
          plan.erase_pending_light = false;
          plan.enqueue_first_mesh = true;
          plan.first_mesh_column = key;
          plan.fsm_after = ColumnEmergeState::Meshing;
        }
      }
      const auto exec_t0 = Clock::now();
      ExecuteLitApplyPlan(plan, key, ground, finalize_pending_gate);
      PhysicsTelemetryData.MarkRelitExecMs += ElapsedMs(exec_t0, Clock::now());
      PhysicsTelemetryData.MarkRelitPrimaryColumnMs +=
          ElapsedMs(primary_t0, Clock::now());
      continue;
    }

    if (primary_only)
    {
      continue;
    }
    if (focus_horiz > 1)
    {
      continue;
    }
    if (PendingLightBeforeMesh.count(key) != 0 || !IsColumnLitReady(ground))
    {
      continue;
    }
    if (enter_quiesce || !MeshService || MeshService->GetDirtyCount() >= 350)
    {
      continue;
    }
    int dirty_min = std::max(0, band.min_y - 1);
    int dirty_max = std::min(column_max_y, band.max_y + 1);
    if (dirty_max < dirty_min)
    {
      continue;
    }
    const auto seam_t0 = Clock::now();
    MeshService->MarkMissingSlicesDirtyPriority(BlockWorld, ground, dirty_min,
                                                dirty_max);
    PhysicsTelemetryData.MarkRelitNeighborSeamMs +=
        ElapsedMs(seam_t0, Clock::now());
  }

  if (MeshService && !relit_chunks.empty() && !primary_only && !moving)
  {
    const auto prefetch_t0 = Clock::now();
    for (const glm::ivec3 &coord : relit_chunks)
    {
      MeshService->PrefetchMeshCapture(GetBlockWorld(), coord);
    }
    PhysicsTelemetryData.MarkRelitPrefetchMs +=
        ElapsedMs(prefetch_t0, Clock::now());
  }

  if (!finalize_pending_gate || !MeshService ||
      ShouldSkipMarkRelitOrphanGround(primary_only, consume_mode))
  {
    PhysicsTelemetryData.MarkRelitTotalMs += ElapsedMs(total_t0, Clock::now());
    return;
  }
  {
    const auto orphan_t0 = Clock::now();
    for (const glm::ivec2 &g : primary_grounds)
    {
      if (bands.count(g) != 0)
      {
        continue;
      }
      AsyncRelightColumnsInFlight.erase(g);
      PendingLightBeforeMesh.erase(g);
      const glm::ivec3 ground(g.x, 0, g.y);
      SetColumnEmergeState(ground, ColumnEmergeState::LitReady);
      const int dirty_min = std::max(0, sea - CHUNK_SIZE);
      const int dirty_max = std::min(column_max_y, sea + CHUNK_SIZE * 2);
      if (priority_mesh)
      {
        MeshService->MarkTerrainChunkMeshDirtySeamedPriority(
            ground, dirty_min, dirty_max,
            /*include_horizontal_neighbors=*/false);
      }
      else
      {
        MeshService->MarkTerrainChunkMeshDirtySeamed(
            ground, dirty_min, dirty_max,
            /*include_horizontal_neighbors=*/false);
      }
      SetColumnEmergeState(ground, ColumnEmergeState::Meshing);
    }
    PhysicsTelemetryData.MarkRelitOrphanGroundMs +=
        ElapsedMs(orphan_t0, Clock::now());
  }
  PhysicsTelemetryData.MarkRelitTotalMs += ElapsedMs(total_t0, Clock::now());
}

} // namespace cutum
