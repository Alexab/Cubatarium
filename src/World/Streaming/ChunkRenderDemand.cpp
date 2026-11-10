#include "World/Streaming/ChunkRenderDemand.h"

#include "Render/Mesh/ChunkMeshFace.h"

#include <algorithm>
#include <chrono>
#include <iterator>

namespace cutum
{

namespace
{
bool StageIsMonotonic(JobStage prev, JobStage next)
{
  if (next == JobStage::Cancelled || next == JobStage::Retired)
  {
    return true;
  }
  return static_cast<uint8_t>(next) >= static_cast<uint8_t>(prev);
}

double DemandEventTimeMs(double requested_ms = 0.0)
{
  // Callers may pass World/flight-relative timestamps. Keep this trace's sort
  // key on one monotonic process-wide clock instead of mixing time domains.
  (void)requested_ms;
  return std::chrono::duration<double, std::milli>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

} // namespace

UChunkRenderDemandStore &UChunkRenderDemandStore::Get()
{
  static UChunkRenderDemandStore instance;
  return instance;
}

UChunkRenderDemandStore::DemandSnapshot
UChunkRenderDemandStore::SnapshotDemand(const ChunkRenderDemandRecord &rec)
{
  return {rec.world_epoch,
          rec.incarnation,
          rec.active_attempt_id,
          rec.desired_geom_rev,
          rec.desired_light_rev,
          rec.desired_coverage_gen,
          rec.published_geom_rev,
          rec.published_light_rev,
          rec.published_coverage_gen,
          rec.active_stage,
          rec.has_active_attempt,
          rec.retained_awaiting_successor,
          rec.face_debt_mask};
}

UChunkRenderDemandStore::UnsatisfiedBreakdown
UChunkRenderDemandStore::BreakdownFor(const DemandSnapshot &snapshot)
{
  UnsatisfiedBreakdown breakdown{};
  if (snapshot.desired_geom_rev != 0 &&
      snapshot.published_geom_rev != snapshot.desired_geom_rev)
  {
    ++breakdown.geom;
  }
  if (snapshot.desired_light_rev != 0 &&
      snapshot.published_light_rev != snapshot.desired_light_rev)
  {
    ++breakdown.light;
  }
  if (snapshot.face_debt_mask != 0)
  {
    ++breakdown.face;
  }
  if (snapshot.desired_coverage_gen != 0 &&
      snapshot.published_coverage_gen < snapshot.desired_coverage_gen)
  {
    ++breakdown.coverage;
  }
  if (snapshot.retained_awaiting_successor)
  {
    ++breakdown.retain;
  }
  return breakdown;
}

void UChunkRenderDemandStore::UpdateCachedUnsatisfiedBreakdown(
    const DemandSnapshot &before, const DemandSnapshot &after)
{
  const UnsatisfiedBreakdown old_breakdown = BreakdownFor(before);
  const UnsatisfiedBreakdown new_breakdown = BreakdownFor(after);
  CachedUnsatisfiedBreakdown_.geom += new_breakdown.geom - old_breakdown.geom;
  CachedUnsatisfiedBreakdown_.light +=
      new_breakdown.light - old_breakdown.light;
  CachedUnsatisfiedBreakdown_.face += new_breakdown.face - old_breakdown.face;
  CachedUnsatisfiedBreakdown_.coverage +=
      new_breakdown.coverage - old_breakdown.coverage;
  CachedUnsatisfiedBreakdown_.retain +=
      new_breakdown.retain - old_breakdown.retain;
}

void UChunkRenderDemandStore::TraceDemandTransition(
    glm::ivec3 coord, const DemandSnapshot &before,
    const ChunkRenderDemandRecord &after, DemandTransitionKind kind,
    double event_ms, uint8_t result)
{
  UpdateCachedUnsatisfiedBreakdown(before, SnapshotDemand(after));

  DemandTransitionSpan span{};
  span.cx = coord.x;
  span.cy = coord.y;
  span.cz = coord.z;
  span.world_epoch = after.world_epoch;
  span.incarnation = after.incarnation;
  span.previous_attempt_id = before.attempt_id;
  span.attempt_id = after.active_attempt_id;
  span.previous_desired_geom_rev = before.desired_geom_rev;
  span.desired_geom_rev = after.desired_geom_rev;
  span.previous_desired_light_rev = before.desired_light_rev;
  span.desired_light_rev = after.desired_light_rev;
  span.previous_desired_coverage_gen = before.desired_coverage_gen;
  span.desired_coverage_gen = after.desired_coverage_gen;
  span.previous_published_geom_rev = before.published_geom_rev;
  span.published_geom_rev = after.published_geom_rev;
  span.previous_published_light_rev = before.published_light_rev;
  span.published_light_rev = after.published_light_rev;
  span.settled_light_rev = after.settled_light_rev;
  span.has_settled_light = after.has_settled_light ? 1 : 0;
  span.previous_published_coverage_gen = before.published_coverage_gen;
  span.published_coverage_gen = after.published_coverage_gen;
  if (kind == DemandTransitionKind::OpenSkyRepairPublished)
  {
    span.open_sky_repair_event_id = after.open_sky_repair_event_id;
    span.open_sky_repair_target_light_rev =
        after.open_sky_repair_target_light_rev;
    span.open_sky_repair_baseline_geom_rev =
        after.open_sky_repair_baseline_geom_rev;
    span.open_sky_repair_baseline_attempt_id =
        after.open_sky_repair_baseline_attempt_id;
    span.open_sky_repair_baseline_published_attempt_id =
        after.open_sky_repair_baseline_published_attempt_id;
    span.open_sky_repair_published_geom_rev =
        after.open_sky_repair_published_geom_rev;
    span.open_sky_repair_published_light_rev =
        after.open_sky_repair_published_light_rev;
    span.open_sky_repair_published_attempt_id =
        after.open_sky_repair_published_attempt_id;
    span.open_sky_repair_pending = after.open_sky_repair_pending ? 1 : 0;
    span.open_sky_repair_published = after.open_sky_repair_published ? 1 : 0;
  }
  span.previous_stage = before.stage;
  span.stage = after.active_stage;
  span.kind = kind;
  span.result = result;
  span.had_active_attempt = before.has_active_attempt ? 1 : 0;
  span.has_active_attempt = after.has_active_attempt ? 1 : 0;
  span.retained_awaiting_successor =
      after.retained_awaiting_successor ? 1 : 0;
  span.event_ms = DemandEventTimeMs(event_ms);
  UJobStageTrace::NoteDemandTransition(span);
}

ChunkRenderDemandRecord *UChunkRenderDemandStore::Find(glm::ivec3 coord)
{
  const auto it = Records_.find(coord);
  return it == Records_.end() ? nullptr : &it->second;
}

const ChunkRenderDemandRecord *
UChunkRenderDemandStore::Find(glm::ivec3 coord) const
{
  const auto it = Records_.find(coord);
  return it == Records_.end() ? nullptr : &it->second;
}

ChunkRenderDemandRecord &
UChunkRenderDemandStore::GetOrCreate(glm::ivec3 coord)
{
  ChunkRenderDemandRecord &rec = Records_[coord];
  rec.coord = coord;
  return rec;
}

void UChunkRenderDemandStore::BindIdentity(glm::ivec3 coord,
                                          uint64_t world_epoch,
                                          uint64_t incarnation)
{
  // An unloaded / absent chunk has no incarnation to bind. World epoch zero is
  // valid in initial worlds, but a live chunk incarnation is always nonzero.
  if (incarnation == 0)
  {
    return;
  }
  auto it = Records_.find(coord);
  if (it == Records_.end())
  {
    ChunkRenderDemandRecord &rec = GetOrCreate(coord);
    rec.world_epoch = world_epoch;
    rec.incarnation = incarnation;
    return;
  }

  ChunkRenderDemandRecord &rec = it->second;
  const bool has_identity = rec.incarnation != 0;
  // A record first created through a legacy/identity-free path must not carry
  // its old coordinate-only state into the first identified chunk instance.
  if (!has_identity || rec.world_epoch != world_epoch ||
      rec.incarnation != incarnation)
  {
    const DemandSnapshot before = SnapshotDemand(rec);
    rec = ChunkRenderDemandRecord{};
    rec.coord = coord;
    rec.world_epoch = world_epoch;
    rec.incarnation = incarnation;
    TraceDemandTransition(coord, before, rec,
                         DemandTransitionKind::IdentityReset);
  }
  rec.world_epoch = world_epoch;
  rec.incarnation = incarnation;
}

void UChunkRenderDemandStore::Remove(glm::ivec3 coord)
{
  const auto it = Records_.find(coord);
  if (it != Records_.end())
  {
    UpdateCachedUnsatisfiedBreakdown(SnapshotDemand(it->second), {});
    Records_.erase(it);
    if (ReconcileCursorValid_ && coord == ReconcileCursorCoord_)
    {
      ReconcileCursorValid_ = false;
    }
  }
}

bool UChunkRenderDemandStore::CoverageSatisfied(
    const ChunkRenderDemandRecord &rec, uint64_t desired_coverage_gen)
{
  if (desired_coverage_gen == 0 && rec.desired_coverage_gen == 0)
  {
    return true;
  }
  const uint64_t need =
      desired_coverage_gen != 0 ? desired_coverage_gen : rec.desired_coverage_gen;
  return need == 0 || rec.published_coverage_gen >= need;
}

bool UChunkRenderDemandStore::PublishedMeetsDesired(
    const ChunkRenderDemandRecord &rec)
{
  if (rec.retained_awaiting_successor)
  {
    return false;
  }
  if (rec.face_debt_mask != 0 || !CoverageSatisfied(rec, 0))
  {
    return false;
  }
  if (rec.desired_geom_rev == 0 && rec.desired_light_rev == 0)
  {
    return true;
  }
  return rec.published_geom_rev == rec.desired_geom_rev &&
         rec.published_light_rev == rec.desired_light_rev;
}

DemandResult UChunkRenderDemandStore::NoteDemand(glm::ivec3 coord,
                                                uint64_t desired_geom_rev,
                                                uint64_t desired_light_rev,
                                                uint64_t desired_coverage_gen,
                                                double now_ms,
                                                uint64_t world_epoch,
                                                uint64_t incarnation)
{
  BindIdentity(coord, world_epoch, incarnation);
  ChunkRenderDemandRecord &rec = GetOrCreate(coord);
  const DemandSnapshot before = SnapshotDemand(rec);
  const bool satisfied =
      !rec.retained_awaiting_successor && desired_geom_rev > 0 &&
      rec.published_geom_rev == desired_geom_rev &&
      rec.published_light_rev == desired_light_rev &&
      CoverageSatisfied(rec, desired_coverage_gen);

  if (satisfied)
  {
    ++AlreadySatisfiedSkipN_;
    return DemandResult::AlreadySatisfied;
  }

  const bool same_desire = rec.desired_geom_rev == desired_geom_rev &&
                           rec.desired_light_rev == desired_light_rev &&
                           (desired_coverage_gen == 0 ||
                            rec.desired_coverage_gen == desired_coverage_gen);
  if (same_desire &&
      (rec.has_active_attempt || rec.desired_geom_rev != 0 ||
       rec.desired_light_rev != 0))
  {
    if (desired_coverage_gen > rec.desired_coverage_gen)
    {
      rec.desired_coverage_gen = desired_coverage_gen;
      if (rec.has_active_attempt)
      {
        TraceDemandTransition(coord, before, rec,
                              DemandTransitionKind::TargetAdvanced,
                              now_ms);
      }
    }
    if (rec.has_active_attempt)
    {
      ++CoalesceN_;
      return DemandResult::Coalesced;
    }
    // A rejected/cancelled attempt leaves its desired stamp intact. Re-admit
    // the same desire with a fresh attempt instead of coalescing into a dead
    // lifecycle record with no owner.
  }

  rec.desired_geom_rev = desired_geom_rev;
  rec.desired_light_rev = desired_light_rev;
  if (desired_coverage_gen > 0)
  {
    rec.desired_coverage_gen = desired_coverage_gen;
  }
  if (!rec.has_active_attempt)
  {
    rec.active_attempt_id = NextAttemptId_++;
    rec.active_stage = JobStage::Created;
    rec.has_active_attempt = true;
    // Progress timestamps belong to an attempt, not to the desired revision.
    // A successor must not inherit its predecessor's progress and thereby
    // evade Created-orphan cleanup or age immediately into the stall timeout.
    rec.last_progress_ms = 0.0;
    // Zero is not an expired timestamp: several legitimate demand producers
    // do not carry a frame clock. Give their Created attempt the same monotonic
    // time domain used by maintenance, so it receives the actual grace period
    // instead of being cancelled on the first reconciliation pass.
    rec.attempt_created_ms = DemandEventTimeMs(now_ms);
  }
  ++NewDemandN_;
  const bool target_changed =
      before.desired_geom_rev != rec.desired_geom_rev ||
      before.desired_light_rev != rec.desired_light_rev ||
      before.desired_coverage_gen != rec.desired_coverage_gen;
  TraceDemandTransition(
      coord, before, rec,
      before.has_active_attempt && target_changed
          ? DemandTransitionKind::TargetAdvanced
          : DemandTransitionKind::AttemptCreated,
      now_ms);
  return DemandResult::NewDemand;
}

bool UChunkRenderDemandStore::NoteStageProgress(glm::ivec3 coord,
                                                JobStage stage,
                                                uint64_t attempt_id,
                                                double now_ms)
{
  // A37 H2: ungated stage writes with authority OFF polluted StopConverged.
  if (!ChunkDemandAuthorityEnabled())
  {
    return false;
  }
  ChunkRenderDemandRecord &rec = GetOrCreate(coord);
  const DemandSnapshot before = SnapshotDemand(rec);
  if (attempt_id != 0)
  {
    if (rec.has_active_attempt && rec.active_attempt_id != 0 &&
        attempt_id != rec.active_attempt_id)
    {
      return false; // foreign attempt — ignore
    }
    if (!rec.has_active_attempt)
    {
      rec.active_attempt_id = attempt_id;
      if (now_ms > 0.0 && rec.attempt_created_ms <= 0.0)
      {
        rec.attempt_created_ms = now_ms;
      }
    }
    else
    {
      rec.active_attempt_id = attempt_id;
    }
  }
  else if (!rec.has_active_attempt)
  {
    rec.active_attempt_id = NextAttemptId_++;
    if (now_ms > 0.0)
    {
      rec.attempt_created_ms = now_ms;
    }
  }
  const bool had_active_attempt = rec.has_active_attempt;
  const JobStage previous_stage = rec.active_stage;
  if (had_active_attempt && !StageIsMonotonic(previous_stage, stage))
  {
    return false;
  }
  rec.has_active_attempt = true;
  rec.active_stage = stage;
  // Repeated queue/admission observations at the same stage are not useful
  // progress. Resetting the watchdog on every scan let a dead Admitted demand
  // live forever even when its slice had no Dirty, worker, or GPU owner.
  const bool stage_advanced = !had_active_attempt || stage != previous_stage;
  if (stage_advanced && now_ms > 0.0)
  {
    rec.last_progress_ms = now_ms;
  }
  if (stage_advanced)
  {
    TraceDemandTransition(coord, before, rec,
                          DemandTransitionKind::StageAdvanced, now_ms);
  }
  return true;
}

void UChunkRenderDemandStore::RebaseDesiredToPublished(glm::ivec3 coord)
{
  ChunkRenderDemandRecord *rec = Find(coord);
  if (!rec)
  {
    return;
  }
  if (rec->published_geom_rev == 0 && rec->published_light_rev == 0)
  {
    return; // nothing published to rebase to — a first-mesh desire stays.
  }
  const DemandSnapshot before = SnapshotDemand(*rec);
  rec->desired_geom_rev = rec->published_geom_rev;
  rec->desired_light_rev = rec->published_light_rev;
  if (rec->has_active_attempt)
  {
    rec->has_active_attempt = false;
    rec->active_stage = JobStage::Cancelled;
  }
  TraceDemandTransition(coord, before, *rec,
                        DemandTransitionKind::TargetAdvanced, 0.0);
}

void UChunkRenderDemandStore::NotePublishedRevs(glm::ivec3 coord,
                                                uint64_t published_geom_rev,
                                                uint64_t published_light_rev)
{
  ChunkRenderDemandRecord &rec = GetOrCreate(coord);
  const DemandSnapshot before = SnapshotDemand(rec);
  const uint64_t old_geom = rec.published_geom_rev;
  const uint64_t old_light = rec.published_light_rev;
  if (published_geom_rev > 0)
  {
    rec.published_geom_rev = published_geom_rev;
  }
  if (published_light_rev > 0)
  {
    rec.published_light_rev = published_light_rev;
  }
  if (old_geom != rec.published_geom_rev ||
      old_light != rec.published_light_rev)
  {
    TraceDemandTransition(coord, before, rec,
                          DemandTransitionKind::PublishedRevisionAdvanced);
  }
}

void UChunkRenderDemandStore::NoteOpenSkyRepairRequired(
    glm::ivec3 coord, uint64_t event_id, uint64_t target_light_rev)
{
  if (event_id == 0)
  {
    return;
  }
  ChunkRenderDemandRecord &rec = GetOrCreate(coord);
  if (rec.open_sky_repair_event_id == event_id &&
      rec.open_sky_repair_target_light_rev == target_light_rev &&
      (rec.open_sky_repair_pending || rec.open_sky_repair_published))
  {
    return;
  }
  rec.open_sky_repair_pending = true;
  rec.open_sky_repair_published = false;
  rec.open_sky_repair_event_id = event_id;
  rec.open_sky_repair_target_light_rev = target_light_rev;
  rec.open_sky_repair_baseline_geom_rev = rec.published_geom_rev;
  rec.open_sky_repair_baseline_attempt_id =
      rec.has_active_attempt ? rec.active_attempt_id : 0;
  rec.open_sky_repair_baseline_published_attempt_id =
      rec.last_published_attempt_id;
  rec.open_sky_repair_published_geom_rev = 0;
  rec.open_sky_repair_published_light_rev = 0;
  rec.open_sky_repair_published_attempt_id = 0;
}

bool UChunkRenderDemandStore::IsOpenSkyRepairPending(
    glm::ivec3 coord, uint64_t event_id, uint64_t target_light_rev) const
{
  const ChunkRenderDemandRecord *rec = Find(coord);
  return rec && rec->open_sky_repair_pending &&
         rec->open_sky_repair_event_id == event_id &&
         rec->open_sky_repair_target_light_rev == target_light_rev;
}

bool UChunkRenderDemandStore::HasOpenSkyRepairPending(
    glm::ivec3 coord, uint64_t target_light_rev) const
{
  const ChunkRenderDemandRecord *rec = Find(coord);
  return rec && rec->open_sky_repair_pending &&
         rec->open_sky_repair_target_light_rev == target_light_rev;
}

bool UChunkRenderDemandStore::IsOpenSkyRepairPublished(
    glm::ivec3 coord, uint64_t event_id, uint64_t target_light_rev) const
{
  const ChunkRenderDemandRecord *rec = Find(coord);
  return rec && !rec->open_sky_repair_pending &&
         rec->open_sky_repair_published &&
         rec->open_sky_repair_event_id == event_id &&
         rec->open_sky_repair_target_light_rev == target_light_rev &&
         rec->open_sky_repair_published_geom_rev >
             rec->open_sky_repair_baseline_geom_rev &&
         rec->open_sky_repair_published_light_rev >= target_light_rev &&
         rec->open_sky_repair_published_attempt_id != 0 &&
         rec->open_sky_repair_published_attempt_id >
             rec->open_sky_repair_baseline_attempt_id &&
         rec->open_sky_repair_published_attempt_id >
             rec->open_sky_repair_baseline_published_attempt_id;
}

bool UChunkRenderDemandStore::HasOpenSkyRepairPublished(
    glm::ivec3 coord, uint64_t target_light_rev) const
{
  const ChunkRenderDemandRecord *rec = Find(coord);
  return rec && IsOpenSkyRepairPublished(
                    coord, rec->open_sky_repair_event_id, target_light_rev);
}

void UChunkRenderDemandStore::NoteLightCalculationSettled(
    glm::ivec3 coord, uint64_t world_epoch, uint64_t incarnation,
    uint64_t light_field_rev)
{
  if (incarnation == 0)
  {
    return;
  }
  BindIdentity(coord, world_epoch, incarnation);
  ChunkRenderDemandRecord &rec = GetOrCreate(coord);
  if (rec.world_epoch != world_epoch || rec.incarnation != incarnation)
  {
    return;
  }
  const bool settlement_changed =
      !rec.has_settled_light || rec.settled_light_rev != light_field_rev;
  const DemandSnapshot before = SnapshotDemand(rec);
  rec.settled_light_rev = light_field_rev;
  rec.has_settled_light = true;
  if (settlement_changed)
  {
    TraceDemandTransition(coord, before, rec,
                          DemandTransitionKind::LightCalculationSettled);
  }
}

void UChunkRenderDemandStore::NoteLifecycleEvent(
    glm::ivec3 coord, DemandTransitionKind kind)
{
  ChunkRenderDemandRecord *rec = Find(coord);
  if (!rec)
  {
    return;
  }
  const DemandSnapshot snapshot = SnapshotDemand(*rec);
  TraceDemandTransition(coord, snapshot, *rec, kind);
}

void UChunkRenderDemandStore::InvalidateLightCalculationSettlement(
    glm::ivec3 coord, uint64_t world_epoch, uint64_t incarnation)
{
  if (incarnation == 0 || !Find(coord))
  {
    return;
  }
  BindIdentity(coord, world_epoch, incarnation);
  ChunkRenderDemandRecord *rec = Find(coord);
  if (!rec || rec->world_epoch != world_epoch ||
      rec->incarnation != incarnation)
  {
    return;
  }
  rec->has_settled_light = false;
  rec->settled_light_rev = 0;
}

bool UChunkRenderDemandStore::NoteInstallResult(glm::ivec3 coord,
                                               InstallResult result,
                                               uint64_t published_geom_rev,
                                               uint64_t published_light_rev,
                                               uint64_t attempt_id,
                                               uint64_t published_coverage_gen)
{
  ChunkRenderDemandRecord &rec = GetOrCreate(coord);
  const DemandSnapshot before = SnapshotDemand(rec);
  // A38 R1: Published must not close an active attempt with attempt_id==0.
  if (result == InstallResult::Published && rec.has_active_attempt &&
      rec.active_attempt_id != 0 &&
      (attempt_id == 0 || attempt_id != rec.active_attempt_id))
  {
    TraceDemandTransition(coord, before, rec,
                          DemandTransitionKind::StaleInstallIgnored, 0.0,
                          static_cast<uint8_t>(result));
    return false;
  }
  if (attempt_id != 0 && rec.has_active_attempt &&
      rec.active_attempt_id != 0 && attempt_id != rec.active_attempt_id)
  {
    TraceDemandTransition(coord, before, rec,
                          DemandTransitionKind::StaleInstallIgnored, 0.0,
                          static_cast<uint8_t>(result));
    return false; // stale completion
  }
  bool open_sky_repair_published_now = false;
  switch (result)
  {
  case InstallResult::Published:
    if (published_geom_rev > 0)
    {
      rec.published_geom_rev = published_geom_rev;
    }
    if (published_light_rev > 0)
    {
      rec.published_light_rev = published_light_rev;
    }
    // A37 H3 / A38 R1: coverage advances only with explicit published_coverage_gen.
    if (published_coverage_gen > 0)
    {
      rec.published_coverage_gen = published_coverage_gen;
    }
    if (attempt_id != 0)
    {
      rec.last_published_attempt_id = attempt_id;
    }
    if (rec.open_sky_repair_pending && attempt_id != 0 &&
        attempt_id > rec.open_sky_repair_baseline_attempt_id &&
        attempt_id > rec.open_sky_repair_baseline_published_attempt_id &&
        published_geom_rev > rec.open_sky_repair_baseline_geom_rev &&
        published_light_rev >= rec.open_sky_repair_target_light_rev)
    {
      rec.open_sky_repair_pending = false;
      rec.open_sky_repair_published = true;
      rec.open_sky_repair_published_geom_rev = published_geom_rev;
      rec.open_sky_repair_published_light_rev = published_light_rev;
      rec.open_sky_repair_published_attempt_id = attempt_id;
      open_sky_repair_published_now = true;
    }
    rec.retained_awaiting_successor = false;
    rec.has_active_attempt = false;
    rec.active_stage = JobStage::Published;
    break;
  case InstallResult::RetainedAwaitingSuccessor:
    rec.retained_awaiting_successor = true;
    rec.has_active_attempt = false;
    rec.active_stage = JobStage::Cancelled;
    ++RetainSuccessorNoteN_;
    break;
  case InstallResult::RejectedRetryable:
    rec.has_active_attempt = false;
    rec.active_stage = JobStage::Cancelled;
    break;
  case InstallResult::CancelledSuperseded:
    rec.has_active_attempt = false;
    rec.retained_awaiting_successor = false;
    rec.active_stage = JobStage::Cancelled;
    break;
  }
  DemandTransitionKind kind = DemandTransitionKind::InstallCancelled;
  switch (result)
  {
  case InstallResult::Published:
    kind = DemandTransitionKind::InstallPublished;
    break;
  case InstallResult::RetainedAwaitingSuccessor:
    kind = DemandTransitionKind::InstallRetained;
    break;
  case InstallResult::RejectedRetryable:
    kind = DemandTransitionKind::InstallRejected;
    break;
  case InstallResult::CancelledSuperseded:
    kind = DemandTransitionKind::InstallCancelled;
    break;
  }
  TraceDemandTransition(coord, before, rec, kind, 0.0,
                        static_cast<uint8_t>(result));
  if (open_sky_repair_published_now)
  {
    TraceDemandTransition(coord, before, rec,
                          DemandTransitionKind::OpenSkyRepairPublished, 0.0,
                          static_cast<uint8_t>(result));
  }
  return true;
}

void UChunkRenderDemandStore::NoteFaceDebt(glm::ivec3 chunk_xyz,
                                          uint8_t face_mask,
                                          uint64_t peer_gen)
{
  if (face_mask == 0)
  {
    face_mask = 0x3Fu;
  }
  ChunkRenderDemandRecord &rec = GetOrCreate(chunk_xyz);
  const DemandSnapshot before = SnapshotDemand(rec);
  rec.peer_face_debt_mask =
      static_cast<uint8_t>(rec.peer_face_debt_mask | face_mask);
  rec.face_debt_mask = static_cast<uint8_t>(
      rec.peer_face_debt_mask | rec.overlay_face_debt_mask);
  if (peer_gen != 0)
  {
    for (int f = 0; f < 6; ++f)
    {
      if ((face_mask & static_cast<uint8_t>(1u << f)) != 0)
      {
        if (peer_gen > rec.waiting_peer_gen[f])
        {
          rec.waiting_peer_gen[f] = peer_gen;
        }
      }
    }
  }
  UpdateCachedUnsatisfiedBreakdown(before, SnapshotDemand(rec));
}

void UChunkRenderDemandStore::NoteBoundaryOverlayDebt(
    glm::ivec3 chunk_xyz, uint8_t face_mask)
{
  if (face_mask == 0)
  {
    return;
  }
  ChunkRenderDemandRecord &rec = GetOrCreate(chunk_xyz);
  const DemandSnapshot before = SnapshotDemand(rec);
  const uint8_t newly_missing = static_cast<uint8_t>(
      face_mask & static_cast<uint8_t>(~rec.overlay_face_debt_mask));
  rec.overlay_face_debt_mask = static_cast<uint8_t>(
      rec.overlay_face_debt_mask | face_mask);
  rec.overlay_repair_attempted_mask = static_cast<uint8_t>(
      rec.overlay_repair_attempted_mask &
      static_cast<uint8_t>(~newly_missing));
  for (int f = 0; f < 6; ++f)
  {
    if ((newly_missing & static_cast<uint8_t>(1u << f)) != 0)
    {
      rec.overlay_repair_peer_incarnation[f] = 0;
      rec.overlay_repair_peer_coverage_gen[f] = 0;
      rec.overlay_repair_target_light_rev[f] = 0;
    }
  }
  rec.face_debt_mask = static_cast<uint8_t>(
      rec.peer_face_debt_mask | rec.overlay_face_debt_mask);
  UpdateCachedUnsatisfiedBreakdown(before, SnapshotDemand(rec));
}

void UChunkRenderDemandStore::NoteBoundaryOverlayPublished(
    glm::ivec3 chunk_xyz, uint8_t missing_face_mask)
{
  ChunkRenderDemandRecord *rec = Find(chunk_xyz);
  if (!rec)
  {
    return;
  }
  const DemandSnapshot before = SnapshotDemand(*rec);
  missing_face_mask = static_cast<uint8_t>(missing_face_mask & 0x3Fu);
  rec->overlay_face_debt_mask = missing_face_mask;
  rec->overlay_repair_attempted_mask = static_cast<uint8_t>(
      rec->overlay_repair_attempted_mask & missing_face_mask);
  rec->face_debt_mask = static_cast<uint8_t>(
      rec->peer_face_debt_mask | rec->overlay_face_debt_mask);
  for (int f = 0; f < 6; ++f)
  {
    const uint8_t bit = static_cast<uint8_t>(1u << f);
    if ((missing_face_mask & bit) == 0)
    {
      rec->overlay_repair_peer_incarnation[f] = 0;
      rec->overlay_repair_peer_coverage_gen[f] = 0;
      rec->overlay_repair_target_light_rev[f] = 0;
    }
    if ((missing_face_mask & bit) == 0 &&
        (rec->peer_face_debt_mask & bit) == 0)
    {
      rec->waiting_peer_gen[f] = 0;
    }
  }
  // A38 R1: closing all face debt publishes desired coverage (stop unlock).
  if (rec->face_debt_mask == 0 && rec->desired_coverage_gen > 0 &&
      rec->published_coverage_gen < rec->desired_coverage_gen)
  {
    rec->published_coverage_gen = rec->desired_coverage_gen;
  }
  UpdateCachedUnsatisfiedBreakdown(before, SnapshotDemand(*rec));
}

bool UChunkRenderDemandStore::CanBeginBoundaryOverlayRepair(
    glm::ivec3 chunk_xyz, int face, uint64_t peer_incarnation,
    uint64_t peer_coverage_gen, uint64_t target_light_rev) const
{
  if (face < 0 || face >= 6)
  {
    return false;
  }
  const ChunkRenderDemandRecord *rec = Find(chunk_xyz);
  const uint8_t bit = static_cast<uint8_t>(1u << face);
  if (!rec || (rec->overlay_face_debt_mask & bit) == 0)
  {
    return false;
  }

  const bool already_attempted =
      (rec->overlay_repair_attempted_mask & bit) != 0 &&
      rec->overlay_repair_peer_incarnation[face] == peer_incarnation &&
      rec->overlay_repair_peer_coverage_gen[face] == peer_coverage_gen &&
      rec->overlay_repair_target_light_rev[face] == target_light_rev;
  return !already_attempted;
}

bool UChunkRenderDemandStore::TryBeginBoundaryOverlayRepair(
    glm::ivec3 chunk_xyz, int face, uint64_t peer_incarnation,
    uint64_t peer_coverage_gen, uint64_t target_light_rev)
{
  if (!CanBeginBoundaryOverlayRepair(chunk_xyz, face, peer_incarnation,
                                     peer_coverage_gen, target_light_rev))
  {
    return false;
  }
  ChunkRenderDemandRecord *rec = Find(chunk_xyz);
  const uint8_t bit = static_cast<uint8_t>(1u << face);

  rec->overlay_repair_attempted_mask = static_cast<uint8_t>(
      rec->overlay_repair_attempted_mask | bit);
  rec->overlay_repair_peer_incarnation[face] = peer_incarnation;
  rec->overlay_repair_peer_coverage_gen[face] = peer_coverage_gen;
  rec->overlay_repair_target_light_rev[face] = target_light_rev;
  return true;
}

void UChunkRenderDemandStore::NoteFaceDebtSatisfied(glm::ivec3 chunk_xyz,
                                                   uint8_t face_mask,
                                                   uint64_t peer_gen)
{
  ChunkRenderDemandRecord *rec = Find(chunk_xyz);
  if (!rec)
  {
    return;
  }
  const DemandSnapshot before = SnapshotDemand(*rec);
  if (face_mask == 0)
  {
    face_mask = 0x3Fu;
  }
  uint8_t clear_mask = 0;
  for (int f = 0; f < 6; ++f)
  {
    const uint8_t bit = static_cast<uint8_t>(1u << f);
    if ((face_mask & bit) == 0)
    {
      continue;
    }
    const uint64_t waiting = rec->waiting_peer_gen[f];
    // A39 P3 / A31-01: UnknownPeer (mask bit + waiting==0) — peer_gen==0
    // must not clear; first real peer publication (peer_gen!=0) may clear.
    if (waiting == 0 && peer_gen == 0)
    {
      continue;
    }
    // A31: peer_gen==0 must not clear a face that waits on a real generation.
    if (waiting != 0 && peer_gen == 0)
    {
      continue;
    }
    if (waiting != 0 && peer_gen < waiting)
    {
      continue; // stale / insufficient peer commit
    }
    clear_mask = static_cast<uint8_t>(clear_mask | bit);
    rec->waiting_peer_gen[f] = 0;
  }
  rec->peer_face_debt_mask = static_cast<uint8_t>(
      rec->peer_face_debt_mask & static_cast<uint8_t>(~clear_mask));
  rec->face_debt_mask = static_cast<uint8_t>(
      rec->peer_face_debt_mask | rec->overlay_face_debt_mask);
  // A38 R1: closing all face debt publishes desired coverage (stop unlock).
  if (rec->face_debt_mask == 0 && rec->desired_coverage_gen > 0 &&
      rec->published_coverage_gen < rec->desired_coverage_gen)
  {
    rec->published_coverage_gen = rec->desired_coverage_gen;
  }
  UpdateCachedUnsatisfiedBreakdown(before, SnapshotDemand(*rec));
}

UChunkRenderDemandStore::ReconcileStats
UChunkRenderDemandStore::ReconcileMaintenance(int max_n, double now_ms)
{
  ReconcileStats stats{};
  if (max_n <= 0 || Records_.empty())
  {
    return stats;
  }
  auto it = Records_.begin();
  if (ReconcileCursorValid_)
  {
    const auto cursor = Records_.find(ReconcileCursorCoord_);
    if (cursor != Records_.end())
    {
      it = std::next(cursor);
      if (it == Records_.end())
      {
        it = Records_.begin();
      }
    }
    else
    {
      ReconcileCursorValid_ = false;
    }
  }
  const size_t max_checked =
      std::min(static_cast<size_t>(max_n), Records_.size());
  size_t checked = 0;
  while (checked < max_checked && it != Records_.end())
  {
    const auto current = it;
    ChunkRenderDemandRecord &rec = it->second;
    ++stats.checked;
    if (rec.desired_geom_rev != rec.published_geom_rev ||
        rec.desired_light_rev != rec.published_light_rev ||
        !CoverageSatisfied(rec, 0) || rec.face_debt_mask != 0)
    {
      if (!(rec.has_active_attempt || rec.retained_awaiting_successor ||
            rec.desired_geom_rev == 0))
      {
        ++stats.mismatch_desired_vs_published;
      }
    }
    if (rec.has_active_attempt &&
        rec.active_stage == JobStage::Created &&
        rec.last_progress_ms <= 0.0)
    {
      const double created = rec.attempt_created_ms;
      const bool past_grace =
          now_ms <= 0.0 || created <= 0.0 ||
          (now_ms - created) >= kOrphanGraceMs;
      if (past_grace)
      {
        ++stats.orphan_active;
        const DemandSnapshot before = SnapshotDemand(rec);
        // Preserve the desired revisions, but do not mint a replacement here:
        // reconciliation has no admission owner and cannot promise executable
        // mesh work. The next producer that reaches NoteDemand will mint a new
        // attempt while it performs the real queue admission.
        rec.has_active_attempt = false;
        rec.active_stage = JobStage::Cancelled;
        TraceDemandTransition(rec.coord, before, rec,
                              DemandTransitionKind::InstallCancelled,
                              now_ms, /*result=*/16);
        // The cancelled desire still leads publication (des > pub); without a
        // Dirty entry no producer ever reaches NoteDemand again and the record
        // freezes (PERF-0.30 witness: des_g climbing while pub_g stuck, queues
        // empty). Report the coord so the caller can re-dirty the chunk.
        if (rec.desired_geom_rev > rec.published_geom_rev ||
            rec.desired_light_rev > rec.published_light_rev)
        {
          if (stats.reclaim_coords.size() < 16)
          {
            stats.reclaim_coords.push_back(rec.coord);
          }
        }
      }
    }
    // A40 P3: stall > SLA → remint one Created attempt (keep desire). Not Kick.
    if (rec.has_active_attempt &&
        (rec.published_geom_rev != rec.desired_geom_rev ||
         rec.published_light_rev != rec.desired_light_rev ||
         !CoverageSatisfied(rec, 0) || rec.face_debt_mask != 0) &&
        now_ms > 0.0 && rec.last_progress_ms > 0.0 &&
        (now_ms - rec.last_progress_ms) > kStallFailMs)
    {
      const DemandSnapshot before = SnapshotDemand(rec);
      rec.has_active_attempt = false;
      rec.active_stage = JobStage::Cancelled;
      if (rec.desired_geom_rev != 0 || rec.desired_light_rev != 0 ||
          rec.desired_coverage_gen != 0)
      {
        rec.active_attempt_id = NextAttemptId_++;
        rec.active_stage = JobStage::Created;
        rec.has_active_attempt = true;
        rec.last_progress_ms = 0.0;
        rec.attempt_created_ms = now_ms;
        ++stats.stalled_reschedule;
      }
      TraceDemandTransition(rec.coord, before, rec,
                            rec.has_active_attempt
                                ? DemandTransitionKind::AttemptReminted
                                : DemandTransitionKind::InstallCancelled,
                            now_ms);
      // A reminted attempt has no queue entry either (A40 P3: "Not Kick");
      // report it for re-dirty like an orphan cancel.
      if (rec.desired_geom_rev > rec.published_geom_rev ||
          rec.desired_light_rev > rec.published_light_rev)
      {
        if (stats.reclaim_coords.size() < 16)
        {
          stats.reclaim_coords.push_back(rec.coord);
        }
      }
    }
    if (rec.retained_awaiting_successor)
    {
      ++stats.retained_awaiting;
    }
    ReconcileCursorCoord_ = current->first;
    ReconcileCursorValid_ = true;
    ++it;
    ++checked;
  }
  if (it == Records_.end())
  {
    ReconcileCursorValid_ = false;
  }
  return stats;
}

int UChunkRenderDemandStore::CancelOrphanActiveAttempts(int max_n,
                                                       double now_ms)
{
  if (max_n <= 0 || Records_.empty())
  {
    return 0;
  }
  int cancelled = 0;
  for (auto &kv : Records_)
  {
    if (cancelled >= max_n)
    {
      break;
    }
    ChunkRenderDemandRecord &rec = kv.second;
    if (rec.has_active_attempt &&
        rec.active_stage == JobStage::Created &&
        rec.last_progress_ms <= 0.0)
    {
      const double created = rec.attempt_created_ms;
      const bool past_grace =
          now_ms <= 0.0 || created <= 0.0 ||
          (now_ms - created) >= kOrphanGraceMs;
      if (!past_grace)
      {
        continue;
      }
      const DemandSnapshot before = SnapshotDemand(rec);
      rec.has_active_attempt = false;
      rec.active_stage = JobStage::Cancelled;
      ++cancelled;
      TraceDemandTransition(kv.first, before, rec,
                            DemandTransitionKind::InstallCancelled, now_ms,
                            /*result=*/16);
    }
  }
  return cancelled;
}

int UChunkRenderDemandStore::CountUnsatisfiedDemands() const
{
  int n = 0;
  for (const auto &kv : Records_)
  {
    const ChunkRenderDemandRecord &rec = kv.second;
    if (rec.desired_geom_rev == 0 && rec.desired_light_rev == 0 &&
        rec.desired_coverage_gen == 0 && rec.face_debt_mask == 0)
    {
      continue;
    }
    if (!PublishedMeetsDesired(rec))
    {
      ++n;
    }
  }
  return n;
}

UChunkRenderDemandStore::UnsatisfiedBreakdown
UChunkRenderDemandStore::CountUnsatisfiedBreakdown() const
{
  return CachedUnsatisfiedBreakdown_;
}

void UChunkRenderDemandStore::SampleUnsatisfiedRecords(
    int max_samples, std::vector<UnsatisfiedSample> &out) const
{
  out.clear();
  if (max_samples <= 0)
  {
    return;
  }
  out.reserve(static_cast<size_t>(max_samples));
  for (const auto &kv : Records_)
  {
    const ChunkRenderDemandRecord &rec = kv.second;
    const bool open = rec.desired_geom_rev != 0 ||
                      rec.desired_light_rev != 0 ||
                      rec.desired_coverage_gen != 0 ||
                      rec.face_debt_mask != 0;
    if (!open)
    {
      continue;
    }
    UnsatisfiedSample sample;
    sample.coord = kv.first;
    sample.desired_geom = rec.desired_geom_rev;
    sample.published_geom = rec.published_geom_rev;
    sample.desired_light = rec.desired_light_rev;
    sample.published_light = rec.published_light_rev;
    sample.desired_coverage = rec.desired_coverage_gen;
    sample.published_coverage = rec.published_coverage_gen;
    sample.face_debt_mask = rec.face_debt_mask;
    sample.retained = rec.retained_awaiting_successor;
    sample.orphan = rec.has_active_attempt &&
                    rec.active_stage == JobStage::Created &&
                    rec.last_progress_ms <= 0.0;
    out.push_back(sample);
    if (static_cast<int>(out.size()) >= max_samples)
    {
      break;
    }
  }
}

int UChunkRenderDemandStore::ClassifyStopBlocker(
    const ChunkRenderDemandRecord &rec, double now_ms,
    const PeerLoadedFn &peer_loaded, const MeshWithheldFn &mesh_withheld) const
{
  if (rec.desired_geom_rev == 0 && rec.desired_light_rev == 0 &&
      rec.desired_coverage_gen == 0 && rec.face_debt_mask == 0)
  {
    return 0;
  }
  // Parked-before-orphan: a record whose only open domain is coverage
  // behind face debts against unloaded peers cannot be serviced until a
  // peer loads. Its orphan/retain lifecycle must not hold the stop — the
  // parked coverage desire is re-raised when a peer publishes.
  // PERF-0.30 witness: coverage-only orphan records at the ring edge
  // blocked StopConverged ahead of the face-debt scoping below.
  if (rec.face_debt_mask != 0)
  {
    const bool parked_revs_behind =
        rec.desired_geom_rev > rec.published_geom_rev ||
        rec.desired_light_rev > rec.published_light_rev;
    const bool parked_coverage_behind =
        rec.desired_coverage_gen > rec.published_coverage_gen;
    if (!parked_revs_behind)
    {
      if (!parked_coverage_behind)
      {
        return 0;
      }
      if (peer_loaded)
      {
        bool any_debt_peer_loaded = false;
        for (int f = 0; f < 6; ++f)
        {
          if ((rec.face_debt_mask & static_cast<uint8_t>(1u << f)) == 0)
          {
            continue;
          }
          if (peer_loaded(rec.coord + ChunkMeshFaceNeighborDelta(f)))
          {
            any_debt_peer_loaded = true;
            break;
          }
        }
        if (!any_debt_peer_loaded)
        {
          return 0;
        }
      }
    }
  }
  // Parked-before-orphan, geometry side: a first-mesh desire the world
  // currently withholds meshing for (soft-defer empty placeholder at the
  // ring edge, stand diet) must not recreate an orphan every admission
  // attempt and then hold the stop (PERF-0.30 witness: des_g=1 pub_g=0
  // orphan cycling while the deferral policy holds the chunk). The
  // frontier re-mints the demand when the deferral lifts.
  if (mesh_withheld && mesh_withheld(rec.coord))
  {
    return 0;
  }
  if (rec.has_active_attempt && rec.active_stage == JobStage::Created &&
      rec.last_progress_ms <= 0.0)
  {
    return 1; // orphan
  }
  // A face debt blocks stop only while it represents an unpublished
  // demand. A debt parked against an unloaded peer with every revision
  // published cannot be serviced during the stop (the peer must load
  // first) and must not hold demand_stop_converged false forever
  // (PERF-0.30 witness: interior zero-quad slices, face_debt_mask=1,
  // des==published geom and coverage, peer outside the loaded ring).
  // The parked debt still drives its repair when the peer publishes.
  if (rec.face_debt_mask != 0)
  {
    const bool revs_behind =
        rec.desired_geom_rev > rec.published_geom_rev ||
        rec.desired_light_rev > rec.published_light_rev;
    const bool coverage_behind =
        rec.desired_coverage_gen > rec.published_coverage_gen;
    if (revs_behind)
    {
      return 2; // face_revs_behind
    }
    if (coverage_behind)
    {
      // Coverage publication needs every debt cleared; a debt whose peer
      // is not loaded cannot clear until the peer loads and publishes.
      // With a loadedness predicate, scope all-unloaded-peer debts out of
      // stop convergence (they park, not block); without one, stay
      // conservative and block (PERF-0.30 witness: des_c=1 pub_c=0,
      // debt faces outside the loaded ring).
      if (!peer_loaded)
      {
        return 3; // coverage_unscoped
      }
      bool any_debt_peer_loaded = false;
      for (int f = 0; f < 6; ++f)
      {
        if ((rec.face_debt_mask & static_cast<uint8_t>(1u << f)) == 0)
        {
          continue;
        }
        if (peer_loaded(rec.coord + ChunkMeshFaceNeighborDelta(f)))
        {
          any_debt_peer_loaded = true;
          break;
        }
      }
      if (any_debt_peer_loaded)
      {
        return 4; // coverage_serviceable
      }
    }
  }
  if (!CoverageSatisfied(rec, 0))
  {
    return 5; // coverage_behind
  }
  if (rec.retained_awaiting_successor)
  {
    // Retain ok only with newer desire AND a live successor attempt.
    if (rec.desired_geom_rev == rec.published_geom_rev &&
        rec.desired_light_rev == rec.published_light_rev)
    {
      return 6; // retain_loop — infinite Retain without successor desire
    }
    if (!rec.has_active_attempt)
    {
      return 7; // retain_no_attempt — desire without live attempt
    }
    return 0;
  }
  if (rec.published_geom_rev != rec.desired_geom_rev ||
      rec.published_light_rev != rec.desired_light_rev)
  {
    if (rec.has_active_attempt)
    {
      if (now_ms > 0.0 && rec.last_progress_ms > 0.0 &&
          (now_ms - rec.last_progress_ms) > kStallFailMs)
      {
        return 9; // stall — stalled in-flight
      }
      return 0; // in-flight ok until stall
    }
    return 8; // no_attempt
  }
  return 0;
}

bool UChunkRenderDemandStore::StopConverged(
    double now_ms, const PeerLoadedFn &peer_loaded,
    const MeshWithheldFn &mesh_withheld) const
{
  for (const auto &kv : Records_)
  {
    if (ClassifyStopBlocker(kv.second, now_ms, peer_loaded, mesh_withheld) !=
        0)
    {
      return false;
    }
  }
  return true;
}

void UChunkRenderDemandStore::CountStopBlockers(
    double now_ms, const PeerLoadedFn &peer_loaded,
    const MeshWithheldFn &mesh_withheld, StopBlockerCensus &out) const
{
  out = StopBlockerCensus{};
  for (const auto &kv : Records_)
  {
    const int blocker =
        ClassifyStopBlocker(kv.second, now_ms, peer_loaded, mesh_withheld);
    if (blocker <= 0 || blocker > 9)
    {
      continue;
    }
    const int index = blocker - 1;
    ++out.counts[index];
    if (out.samples[index].size() < 3)
    {
      out.samples[index].push_back(kv.first);
    }
  }
}

void UChunkRenderDemandStore::Clear()
{
  Records_.clear();
  CachedUnsatisfiedBreakdown_ = {};
  ReconcileCursorValid_ = false;
}

} // namespace cutum
