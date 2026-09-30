#include "World/Streaming/ChunkRenderDemand.h"

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

struct DemandSnapshot
{
  uint64_t world_epoch{0};
  uint64_t incarnation{0};
  uint64_t attempt_id{0};
  uint64_t desired_geom_rev{0};
  uint64_t desired_light_rev{0};
  uint64_t desired_coverage_gen{0};
  uint64_t published_geom_rev{0};
  uint64_t published_light_rev{0};
  uint64_t published_coverage_gen{0};
  JobStage stage{JobStage::Created};
  bool has_active_attempt{false};
  bool retained_awaiting_successor{false};
};

DemandSnapshot SnapshotDemand(const ChunkRenderDemandRecord &rec)
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
          rec.retained_awaiting_successor};
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

void TraceDemandTransition(glm::ivec3 coord,
                           const DemandSnapshot &before,
                           const ChunkRenderDemandRecord &after,
                           DemandTransitionKind kind, double event_ms = 0.0,
                           uint8_t result = 0)
{
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
  span.previous_published_coverage_gen = before.published_coverage_gen;
  span.published_coverage_gen = after.published_coverage_gen;
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
} // namespace

UChunkRenderDemandStore &UChunkRenderDemandStore::Get()
{
  static UChunkRenderDemandStore instance;
  return instance;
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
  Records_.erase(coord);
  ReconcileCursor_ = 0;
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
    rec.attempt_created_ms = now_ms > 0.0 ? now_ms : 0.0;
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
  rec.settled_light_rev = light_field_rev;
  rec.has_settled_light = true;
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
}

void UChunkRenderDemandStore::NoteBoundaryOverlayDebt(
    glm::ivec3 chunk_xyz, uint8_t face_mask)
{
  if (face_mask == 0)
  {
    return;
  }
  ChunkRenderDemandRecord &rec = GetOrCreate(chunk_xyz);
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
}

void UChunkRenderDemandStore::NoteBoundaryOverlayPublished(
    glm::ivec3 chunk_xyz, uint8_t missing_face_mask)
{
  ChunkRenderDemandRecord *rec = Find(chunk_xyz);
  if (!rec)
  {
    return;
  }
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
}

UChunkRenderDemandStore::ReconcileStats
UChunkRenderDemandStore::ReconcileMaintenance(int max_n, double now_ms)
{
  ReconcileStats stats{};
  if (max_n <= 0 || Records_.empty())
  {
    return stats;
  }
  if (ReconcileCursor_ >= Records_.size())
  {
    ReconcileCursor_ = 0;
  }
  auto it = Records_.begin();
  std::advance(it, static_cast<std::ptrdiff_t>(
                       std::min(ReconcileCursor_, Records_.size())));
  int n = 0;
  while (n < max_n && it != Records_.end())
  {
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
                              now_ms);
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
    }
    if (rec.retained_awaiting_successor)
    {
      ++stats.retained_awaiting;
    }
    ++it;
    ++n;
    ++ReconcileCursor_;
  }
  if (it == Records_.end())
  {
    ReconcileCursor_ = 0;
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
                            DemandTransitionKind::InstallCancelled, now_ms);
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
  UnsatisfiedBreakdown b{};
  for (const auto &kv : Records_)
  {
    const ChunkRenderDemandRecord &rec = kv.second;
    if (rec.desired_geom_rev == 0 && rec.desired_light_rev == 0 &&
        rec.desired_coverage_gen == 0 && rec.face_debt_mask == 0 &&
        !rec.retained_awaiting_successor)
    {
      continue;
    }
    if (rec.desired_geom_rev != 0 &&
        rec.published_geom_rev != rec.desired_geom_rev)
    {
      ++b.geom;
    }
    if (rec.desired_light_rev != 0 &&
        rec.published_light_rev != rec.desired_light_rev)
    {
      ++b.light;
    }
    if (rec.face_debt_mask != 0)
    {
      ++b.face;
    }
    if (!CoverageSatisfied(rec, 0))
    {
      ++b.coverage;
    }
    if (rec.retained_awaiting_successor)
    {
      ++b.retain;
    }
  }
  return b;
}

bool UChunkRenderDemandStore::StopConverged(double now_ms) const
{
  for (const auto &kv : Records_)
  {
    const ChunkRenderDemandRecord &rec = kv.second;
    if (rec.has_active_attempt &&
        rec.active_stage == JobStage::Created &&
        rec.last_progress_ms <= 0.0)
    {
      return false; // orphan
    }
    if (rec.desired_geom_rev == 0 && rec.desired_light_rev == 0 &&
        rec.desired_coverage_gen == 0 && rec.face_debt_mask == 0)
    {
      continue;
    }
    if (rec.face_debt_mask != 0)
    {
      return false;
    }
    if (!CoverageSatisfied(rec, 0))
    {
      return false;
    }
    if (rec.retained_awaiting_successor)
    {
      // Retain ok only with newer desire AND a live successor attempt.
      if (rec.desired_geom_rev == rec.published_geom_rev &&
          rec.desired_light_rev == rec.published_light_rev)
      {
        return false; // infinite Retain without successor desire
      }
      if (!rec.has_active_attempt)
      {
        return false; // desire raised but no live attempt
      }
      continue;
    }
    if (rec.published_geom_rev != rec.desired_geom_rev ||
        rec.published_light_rev != rec.desired_light_rev)
    {
      if (rec.has_active_attempt)
      {
        if (now_ms > 0.0 && rec.last_progress_ms > 0.0 &&
            (now_ms - rec.last_progress_ms) > kStallFailMs)
        {
          return false; // stalled in-flight
        }
        continue; // in-flight ok until stall
      }
      return false;
    }
  }
  return true;
}

void UChunkRenderDemandStore::Clear()
{
  Records_.clear();
  ReconcileCursor_ = 0;
}

} // namespace cutum
