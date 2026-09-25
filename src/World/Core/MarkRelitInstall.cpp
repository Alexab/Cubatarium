#include "World/Core/World.h"

#include "World/Chunks/Chunk.h"
#include "World/Diagnostics/JobStageTrace.h"
#include "World/Mesh/WorldMeshService.h"
#include "World/Persistence/WorldPersistence.h"
#include "World/Streaming/AntiFlickerPolicy.h"
#include "World/Streaming/ChunkRenderDemand.h"
#include "World/Streaming/ColumnFlowExecutor.h"
#include "World/Streaming/ColumnRecord.h"
#include "World/Streaming/ColumnVisualState.h"
#include "World/Streaming/EnterVisualWarmupPolicy.h"
#include "World/Streaming/MeshLightStalePolicy.h"
#include "World/Streaming/RelightFifoPolicy.h"
#include "World/Streaming/RelightInstallPlanner.h"
#include "World/Streaming/VisualStagePolicy.h"
#include "World/Streaming/VisualObligationPolicy.h"

#include "Render/Mesh/MeshCaptureWorker.h"

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
    // N04 H3: GPU dark-face still_stale only feeds force via planner when
    // ticket drained; equal-rev alone is not automatic remesh demand.
    if (!snap.still_stale && snap.fully_dark)
    {
      snap.still_stale = IsMeshLightStaleGpu(
          probe.gpu_resident, probe.gpu_has_dark_face, snap.meshed_light_rev,
          snap.light_field_rev);
    }
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
  ColumnRecord &col_rec = GetColumnRecords().GetOrCreate(column);
  for (const glm::ivec3 &coord : plan.prefer_kick_gpu)
  {
    mesh->PreferKickPendingGpuQueued(coord);
    ++PhysicsTelemetryData.MarkRelitPreferKickN;
  }
  // Ownership LightConverge: PreferKick does NOT NotePublishProgress /
  // Publishing (v3 honesty / dd7871ab). Progress only after Dirty admit below.
  if (plan.note_prefer_kick_stall)
  {
    GetColumnRecords().NotePreferKickStall(column);
  }
  for (const glm::ivec3 &coord : plan.request_raa)
  {
    mesh->RequestRemeshAfterApply(coord);
    ++PhysicsTelemetryData.MarkRelitRemeshAfterApplyN;
  }
  if (!plan.mark_dirty_priority.empty() || !plan.mark_dirty.empty())
  {
    const auto dirty_t0 = Clock::now();
    const glm::ivec3 focus_block = GetPreferredLoadFocusBlock();
    const glm::ivec3 focus_g = UChunkManager::WorldToChunk(focus_block);
    int dirty_admitted_n = 0;
    auto admit_dirty = [&](const glm::ivec3 &coord, bool priority) {
      bool recent_draw_gate_reject = false;
      bool visible_remesh_priority = false;
      const int horiz = std::max(std::abs(coord.x - focus_g.x),
                                 std::abs(coord.z - focus_g.z));
      const double demand_now_ms = VisualObligationNowMs();
      // A21 P2.1/P2.2: shadow demand — skip MarkDirty when published meets desire.
      if (kChunkDemandShadow())
      {
        uint64_t desired_geom = 0;
        uint64_t desired_light = 0;
        uint64_t incarnation = 0;
        if (const UChunk *ch = BlockWorld.GetChunkManager().GetChunk(coord))
        {
          // Demand geom rev shares the MeshRevisions domain used by Published.
          desired_geom = mesh->GetChunkMeshRevision(coord);
          desired_light = ch->GetLightFieldRevision();
          incarnation = ch->GetIncarnation();
        }
        UChunkRenderDemandStore &demand = UChunkRenderDemandStore::Get();
        const uint64_t world_epoch =
            mesh->GetCache().GetCaptureStore().WorldEpoch();
        demand.BindIdentity(coord, world_epoch, incarnation);
        const MeshPublishRevs pub = mesh->GetCache().GetMeshPublishRevs(coord);
        uint64_t pub_geom = pub.geom_rev;
        uint64_t pub_light = pub.light_rev;
        if (pub_light == 0)
        {
          UChunkMeshCache::LitApplyMeshProbe probe{};
          mesh->FillLitApplyMeshProbe(coord, probe);
          if (probe.meshed_light_rev != 0)
          {
            pub_light = probe.meshed_light_rev;
          }
        }
        demand.NotePublishedRevs(coord, pub_geom, pub_light);
        const DemandResult dr =
            demand.NoteDemand(coord, desired_geom, desired_light,
                              /*desired_coverage_gen=*/0, /*now_ms=*/0.0,
                              world_epoch, incarnation);
        // A21 residual R2 / A37 H4: FullyDark is light desire, not geometry miss.
        UChunkMeshCache::LitApplyMeshProbe dark_probe{};
        mesh->FillLitApplyMeshProbe(coord, dark_probe);
        const bool fully_dark_drawable =
            dark_probe.has_drawable &&
            (dark_probe.fully_dark || dark_probe.gpu_has_dark_face ||
             mesh->GetCache().ChunkHasFullyDarkFace(coord));
        if (dr == DemandResult::AlreadySatisfied && !fully_dark_drawable)
        {
          ++PhysicsTelemetryData.DemandAlreadySatisfiedSkipN;
          return;
        }
        if (dr == DemandResult::AlreadySatisfied && fully_dark_drawable)
        {
          // A41: open_sky equal-rev FD → LightRepair Dirty once (never +1).
          // Cave (!open_sky) → RelightOnly / LegalDark path (no Dirty invent).
          const glm::ivec2 col_xz(coord.x, coord.z);
          const bool open_sky =
              EnterVisualGateCtrl.WasOpenSkyApplied(col_xz);
          mesh->GetCache().InvalidateMeshCapture(coord);
          if (open_sky)
          {
            ColumnRecord &orec = GetColumnRecords().GetOrCreate(col_xz);
            orec.legal_dark_settled = false;
            orec.visual_obligation = VisualObligation::LightRepair;
            const bool live_pipeline = LightRepairHasLivePipeline(
                mesh->IsRemeshAfterApplyPending(coord),
                mesh->IsPendingGpuApply(coord),
                mesh->HasInflightMeshBuild(coord));
            const double now_ms = VisualObligationNowMs();
            if (ShouldRemintLightRepairDirty(
                    /*obligation=*/true, live_pipeline, orec.visual_attempt_id,
                    orec.visual_deadline_ms, now_ms))
            {
              static uint64_t next_lr_attempt = 1;
              orec.visual_attempt_id = next_lr_attempt++;
              orec.visual_deadline_ms = StampLightRepairDeadlineMs(now_ms);
              // Fall through to MarkDirty below (do not return).
            }
            else
            {
              ++PhysicsTelemetryData.MarkRelitScheduleN;
              return;
            }
          }
          else if (Persistence)
          {
            Persistence->EnqueueTerrainColumnRelight(
                coord.x * CHUNK_SIZE, coord.z * CHUNK_SIZE, /*priority=*/true,
                coord.y * CHUNK_SIZE, (coord.y + 1) * CHUNK_SIZE - 1);
            ++PhysicsTelemetryData.MarkRelitScheduleN;
            return;
          }
          else
          {
            return;
          }
        }
      }
      // Sysreset v5: hinterland drops when admit dry; focus horiz≤4 always
      // enqueues (PreferKick≡0 cannot own pending GPU alone).
      if (!mesh->TryConsumeDirtyAdmit())
      {
        if (horiz > 4)
        {
          // A28 T1: hinterland admit-deny is not a Dirty queue drop — omit
          // DirtyDropped so A24 thrash gate measures DropRemesh/MaybeDrop only.
          return;
        }
        // Focus bypass: still MarkDirty* without consuming admit.
      }
      // A23 D0/D1: equal-rev CaptureStore hit re-bakes the same dark vertices —
      // Invalidate so the next build re-reads GetLightData().
      {
        UChunkMeshCache::LitApplyMeshProbe dark_probe{};
        mesh->FillLitApplyMeshProbe(coord, dark_probe);
        if (dark_probe.has_drawable &&
            (dark_probe.fully_dark || dark_probe.gpu_has_dark_face ||
             mesh->GetCache().ChunkHasFullyDarkFace(coord)))
        {
          mesh->GetCache().InvalidateMeshCapture(coord);
        }
      }
      if (priority)
      {
        mesh->MarkDirtyPriority(coord);
        recent_draw_gate_reject =
            WasRecentlyRendererDrawGateRejected(coord);
        if (recent_draw_gate_reject)
        {
          visible_remesh_priority =
              mesh->GetCache().PrioritizeVisibleLightRepairRemesh(coord);
        }
        ++PhysicsTelemetryData.FmDirtyEnqueueN;
        ++PhysicsTelemetryData.FmDirtyEnqueueFromMarkRelitN;
      }
      else
      {
        mesh->MarkDirty(coord);
      }
      ++PhysicsTelemetryData.MarkRelitScheduleN;
      ++dirty_admitted_n;
      {
        JobStageSpan span{};
        span.cx = coord.x;
        span.cy = coord.y;
        span.cz = coord.z;
        span.stage = JobStage::Admitted;
        span.queue_reason = priority ? 1 : 0;
        span.outcome = visible_remesh_priority
                           ? 1u
                           : (recent_draw_gate_reject ? 2u : 0u);
        span.stage_ms = ElapsedMs(dirty_t0, Clock::now());
        // A23 D0: always stamp desired/source revs on Admit (even when shadow OFF).
        if (const UChunk *ch = BlockWorld.GetChunkManager().GetChunk(coord))
        {
          span.desired_rev = ch->GetLightFieldRevision();
          span.source_rev = mesh->GetChunkMeshRevision(coord);
          span.desired_light_rev = ch->GetLightFieldRevision();
          span.source_geom_rev = span.source_rev;
          span.source_light_rev = ch->GetLightFieldRevision();
        }
        {
          const MeshPublishRevs pub = mesh->GetCache().GetMeshPublishRevs(coord);
          if (pub.light_rev != 0)
          {
            span.published_rev = pub.light_rev;
            span.published_light_rev = pub.light_rev;
          }
          else
          {
            UChunkMeshCache::LitApplyMeshProbe probe{};
            mesh->FillLitApplyMeshProbe(coord, probe);
            span.published_rev = probe.meshed_light_rev;
            span.published_light_rev = probe.meshed_light_rev;
          }
        }
        // A25 R1 / A31: stamp demand-store desired/published + active attempt.
        {
          UChunkRenderDemandStore &demand = UChunkRenderDemandStore::Get();
          uint64_t attempt_id = 0;
          if (const ChunkRenderDemandRecord *drec = demand.Find(coord))
          {
            attempt_id = drec->active_attempt_id;
            (void)StampChunkRenderDemandTrace(span, demand, coord);
            if (drec->desired_light_rev != 0)
            {
              span.desired_rev = drec->desired_light_rev;
              span.desired_light_rev = drec->desired_light_rev;
            }
            if (drec->published_light_rev != 0)
            {
              span.published_rev = drec->published_light_rev;
              span.published_light_rev = drec->published_light_rev;
            }
          }
          span.attempt_id = attempt_id;
          UJobStageTrace::Note(span);
          // Keep lifecycle clocks on the VisualObligationNowMs timeline;
          // span.stage_ms is only the duration of this MarkRelit operation.
          demand.NoteStageProgress(coord, JobStage::Admitted, attempt_id,
                                   demand_now_ms);
        }
      }
    };
    for (const glm::ivec3 &coord : plan.mark_dirty_priority)
    {
      admit_dirty(coord, /*priority=*/true);
    }
    for (const glm::ivec3 &coord : plan.mark_dirty)
    {
      admit_dirty(coord, /*priority=*/false);
    }
    PhysicsTelemetryData.MarkRelitMarkDirtyMs +=
        ElapsedMs(dirty_t0, Clock::now());
    // Progress honesty: only when ≥1 Dirty actually admitted.
    if (dirty_admitted_n > 0)
    {
      GetColumnRecords().NotePublishProgress(column);
      if (col_rec.visual == ColumnVisualState::NeedRelight ||
          col_rec.visual == ColumnVisualState::NeedRemesh ||
          col_rec.visual == ColumnVisualState::Ready)
      {
        col_rec.visual = ColumnVisualState::Publishing;
      }
    }
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
    if (plan.fsm_after == ColumnEmergeState::LitReady &&
        col_rec.visual == ColumnVisualState::Publishing)
    {
      col_rec.visual = ColumnVisualState::Ready;
      col_rec.publish_progress_frames = 0;
      col_rec.prefer_kick_stall_frames = 0;
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
        if (const ColumnRecord *rec = GetColumnRecords().Find(key))
        {
          snap.has_publish_progress = rec->publish_progress_frames > 0;
          snap.prefer_kick_stall_frames = rec->prefer_kick_stall_frames;
          snap.visual = rec->visual;
        }
        if (snap.fully_dark && snap.has_drawable)
        {
          ColumnRecord &rec = GetColumnRecords().GetOrCreate(key);
          if (rec.visual == ColumnVisualState::Ready ||
              rec.visual == ColumnVisualState::PublishedEmpty)
          {
            rec.visual = ColumnVisualState::NeedRelight;
          }
          snap.visual = rec.visual;
        }
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
      // Phase 5.7R6: do not damp SoftDefer-empty remesh under focus lit carve.
      const bool carve_lit = ShouldCarveFocusLitCompletion(
          PhysicsTelemetryData.RelightFifoN,
          PhysicsTelemetryData.FocusMissingMesh != 0,
          PhysicsTelemetryData.MissHoriz,
          /*pending_light_near=*/true);
      in.damp_soft_empty_remesh =
          !carve_lit &&
          ShouldDampMarkRelitRemeshOnSoftDeferEmpty(
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
      // A41: Relight terminal — exclusive LegalDark vs LightRepair.
      // - !open_sky equal-rev FD → LegalDark (stamp, clear PL, no Dirty)
      // - open_sky equal-rev FD → LightRepair (keep PL, one Dirty, never +1)
      // - still_stale → LightRepair (keep PL, Relight/Dirty via force_stale)
      // REPLACE cooldown equal-rev force_stale (A24) — LightRepair owns remesh.
      {
        ColumnRecord &rec = GetColumnRecords().GetOrCreate(key);
        const bool open_sky = EnterVisualGateCtrl.WasOpenSkyApplied(key);
        const bool lit_or_lighting =
            IsColumnLitReady(ground) ||
            GetColumnEmergeState(ground) == ColumnEmergeState::Lighting;
        const bool equal_rev_fd = any_fully_dark && !any_still_stale;
        in.light_repair_once = false;
        if (equal_rev_fd && lit_or_lighting)
        {
          if (IsLegalDarkEqualRevFullyDark(/*fully_dark=*/true,
                                           /*still_stale=*/false, open_sky))
          {
            rec.legal_dark_settled = true;
            rec.visual_obligation = VisualObligation::LegalDark;
            rec.visual_attempt_id = 0;
            rec.visual_deadline_ms = 0;
            in.column_settled = true;
          }
          else if (NeedsOpenSkyEqualRevLightRepair(/*fully_dark=*/true,
                                                   /*still_stale=*/false,
                                                   open_sky))
          {
            rec.legal_dark_settled = false;
            rec.visual_obligation = VisualObligation::LightRepair;
            in.column_settled = false;
            bool live_pipeline = false;
            for (const ColumnChunkSnapshot &snap : in.relit_chunks)
            {
              if (LightRepairHasLivePipeline(snap.raa_pending, snap.gpu_pending,
                                             snap.inflight))
              {
                live_pipeline = true;
                break;
              }
            }
            const double now_ms = VisualObligationNowMs();
            if (ShouldRemintLightRepairDirty(
                    /*obligation=*/true, live_pipeline, rec.visual_attempt_id,
                    rec.visual_deadline_ms, now_ms))
            {
              for (const ColumnChunkSnapshot &snap : in.relit_chunks)
              {
                if (snap.fully_dark && MeshService)
                {
                  MeshService->GetCache().InvalidateMeshCapture(snap.coord);
                }
              }
              in.light_repair_once = true;
              static uint64_t next_visual_attempt = 1;
              rec.visual_attempt_id = next_visual_attempt++;
              rec.visual_deadline_ms = StampLightRepairDeadlineMs(now_ms);
              ++PhysicsTelemetryData.MarkRelitScheduleN;
            }
          }
        }
        else if (any_still_stale)
        {
          rec.legal_dark_settled = false;
          rec.visual_obligation = VisualObligation::LightRepair;
          in.column_settled = false;
          // A41: open_sky still_stale without live pipeline → SLA Dirty remint
          // (Relight-only was starving when fifo empty / Dirty stuck).
          if (open_sky)
          {
            bool live_pipeline = false;
            for (const ColumnChunkSnapshot &snap : in.relit_chunks)
            {
              if (LightRepairHasLivePipeline(snap.raa_pending, snap.gpu_pending,
                                             snap.inflight))
              {
                live_pipeline = true;
                break;
              }
            }
            const double now_ms = VisualObligationNowMs();
            if (ShouldRemintLightRepairDirty(
                    /*obligation=*/true, live_pipeline, rec.visual_attempt_id,
                    rec.visual_deadline_ms, now_ms))
            {
              for (const ColumnChunkSnapshot &snap : in.relit_chunks)
              {
                if (snap.fully_dark && MeshService)
                {
                  MeshService->GetCache().InvalidateMeshCapture(snap.coord);
                }
              }
              in.light_repair_once = true;
              static uint64_t next_stale_attempt = 1;
              rec.visual_attempt_id = next_stale_attempt++;
              rec.visual_deadline_ms = StampLightRepairDeadlineMs(now_ms);
              ++PhysicsTelemetryData.MarkRelitScheduleN;
            }
          }
        }
        else if (!any_fully_dark)
        {
          rec.legal_dark_settled = false;
          rec.visual_obligation = VisualObligation::LitDrawable;
          rec.visual_attempt_id = 0;
          rec.visual_deadline_ms = 0;
          in.column_settled = true;
        }
        else
        {
          rec.legal_dark_settled = false;
          in.column_settled = false;
        }
      }
      // A41: do not call ShouldCooldownForceEqualRevPendingFullyDark (replaced by
      // LightRepair). Keep ticketed still_stale force only.
      if (in.force_stale_ticket)
      {
        ++PhysicsTelemetryData.MarkRelitForceStaleN;
      }
      {
        const bool repair_progress = ColumnHasRepairProgress(key);
        if (in.has_repair_ticket && !repair_progress && any_fully_dark)
        {
          ++PhysicsTelemetryData.MarkRelitHitStalledN;
        }
      }

      const auto plan_t0 = Clock::now();
      LitApplyPlan plan = PlanColumnInstall(in);
      // A41: keep PL while LightRepair; clear on LegalDark / LitDrawable.
      {
        const bool open_sky = EnterVisualGateCtrl.WasOpenSkyApplied(key);
        const bool light_repair =
            any_still_stale || in.light_repair_once ||
            NeedsOpenSkyEqualRevLightRepair(any_fully_dark, any_still_stale,
                                            open_sky);
        if (light_repair)
        {
          plan.erase_pending_light = false;
          if (any_still_stale && Persistence &&
              AsyncRelightColumnsInFlight.count(key) == 0)
          {
            Persistence->EnqueueTerrainColumnRelight(
                key.x * CHUNK_SIZE, key.y * CHUNK_SIZE, /*priority=*/true,
                band.min_y, band.max_y);
            ++PhysicsTelemetryData.MarkRelitScheduleN;
          }
        }
        else if (ShouldClearPendingAfterRelightTerminal(
                     IsColumnLitReady(ground) ||
                         GetColumnEmergeState(ground) ==
                             ColumnEmergeState::Lighting,
                     /*all_slices_terminal=*/!any_still_stale,
                     any_still_stale))
        {
          plan.erase_pending_light = true;
        }
      }
      // Audit16 S5: H2 ticketed FullyDark remesh Dirty deleted (sole-owner).
      // Census remesh FREEZE; MarkRelit RelightReplace remains Dirty owner.
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
        plan.enqueue_first_mesh = true;
        plan.first_mesh_column = key;
        plan.fsm_after = ColumnEmergeState::Meshing;
        ++PhysicsTelemetryData.AdmitCandidatesN;
        if (!in.has_fm_ticket)
        {
          plan.erase_pending_light = false;
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
    (void)dirty_min;
    (void)dirty_max;
    const auto seam_t0 = Clock::now();
    if (!GetColumnFlowExecutor().Scheduler().Contains(key,
                                                     ColumnWorkKind::FirstMesh))
    {
      GetColumnFlowExecutor().Enqueue(key, ColumnWorkKind::FirstMesh,
                                      /*priority=*/65);
    }
    if (UMeshCaptureWorker::kWorkerCaptureEnabled && MeshService)
    {
      MeshService->PrefetchMeshCapture(GetBlockWorld(), ground);
    }
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
  if (kChunkDemandShadow())
  {
    const auto recon =
        UChunkRenderDemandStore::Get().ReconcileMaintenance(
            /*max_n=*/32, VisualObligationNowMs());
    PhysicsTelemetryData.DemandReconcileMismatchN +=
        recon.mismatch_desired_vs_published;
    PhysicsTelemetryData.DemandShadowMismatchN =
        UChunkRenderDemandStore::Get().ShadowMismatchN();
  }
}

} // namespace cutum
