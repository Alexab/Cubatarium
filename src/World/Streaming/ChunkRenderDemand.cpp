#include "World/Streaming/ChunkRenderDemand.h"

#include <algorithm>
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
                                                double now_ms)
{
  ChunkRenderDemandRecord &rec = GetOrCreate(coord);
  const bool satisfied =
      !rec.retained_awaiting_successor && desired_geom_rev > 0 &&
      desired_light_rev > 0 && rec.published_geom_rev == desired_geom_rev &&
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
    }
    ++CoalesceN_;
    return DemandResult::Coalesced;
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
    if (now_ms > 0.0)
    {
      rec.attempt_created_ms = now_ms;
    }
  }
  ++NewDemandN_;
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
  if (rec.has_active_attempt &&
      !StageIsMonotonic(rec.active_stage, stage))
  {
    return false;
  }
  rec.has_active_attempt = true;
  rec.active_stage = stage;
  if (now_ms > 0.0)
  {
    rec.last_progress_ms = now_ms;
  }
  return true;
}

void UChunkRenderDemandStore::NotePublishedRevs(glm::ivec3 coord,
                                                uint64_t published_geom_rev,
                                                uint64_t published_light_rev)
{
  ChunkRenderDemandRecord &rec = GetOrCreate(coord);
  if (published_geom_rev > 0)
  {
    rec.published_geom_rev = published_geom_rev;
  }
  if (published_light_rev > 0)
  {
    rec.published_light_rev = published_light_rev;
  }
}

bool UChunkRenderDemandStore::NoteInstallResult(glm::ivec3 coord,
                                               InstallResult result,
                                               uint64_t published_geom_rev,
                                               uint64_t published_light_rev,
                                               uint64_t attempt_id,
                                               uint64_t published_coverage_gen)
{
  ChunkRenderDemandRecord &rec = GetOrCreate(coord);
  if (attempt_id != 0 && rec.has_active_attempt &&
      rec.active_attempt_id != 0 && attempt_id != rec.active_attempt_id)
  {
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
    // A37 H3: coverage advances only with explicit published_coverage_gen.
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
  rec.face_debt_mask =
      static_cast<uint8_t>(rec.face_debt_mask | face_mask);
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
  rec->face_debt_mask =
      static_cast<uint8_t>(rec->face_debt_mask &
                           static_cast<uint8_t>(~clear_mask));
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
        // Re-admit desire: keep desire, clear orphan attempt so NoteDemand can
        // mint a successor rather than silent cancel of obligation.
        rec.has_active_attempt = false;
        rec.active_stage = JobStage::Cancelled;
        if (rec.desired_geom_rev != 0 || rec.desired_light_rev != 0)
        {
          rec.active_attempt_id = NextAttemptId_++;
          rec.active_stage = JobStage::Created;
          rec.has_active_attempt = true;
          if (now_ms > 0.0)
          {
            rec.attempt_created_ms = now_ms;
          }
        }
      }
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
      rec.has_active_attempt = false;
      rec.active_stage = JobStage::Cancelled;
      ++cancelled;
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
