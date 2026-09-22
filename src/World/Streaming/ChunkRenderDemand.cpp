#include "World/Streaming/ChunkRenderDemand.h"

#include <algorithm>
#include <iterator>

namespace cutum
{

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

DemandResult UChunkRenderDemandStore::NoteDemand(glm::ivec3 coord,
                                                uint64_t desired_geom_rev,
                                                uint64_t desired_light_rev,
                                                uint64_t desired_coverage_gen)
{
  ChunkRenderDemandRecord &rec = GetOrCreate(coord);
  // Explicit equality on published vs desired when both sides known.
  const bool satisfied =
      !rec.retained_awaiting_successor && desired_geom_rev > 0 &&
      desired_light_rev > 0 && rec.published_geom_rev == desired_geom_rev &&
      rec.published_light_rev == desired_light_rev &&
      (desired_coverage_gen == 0 ||
       desired_coverage_gen <= rec.desired_coverage_gen);

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
  }
  ++NewDemandN_;
  return DemandResult::NewDemand;
}

void UChunkRenderDemandStore::NoteStageProgress(glm::ivec3 coord,
                                                JobStage stage,
                                                uint64_t attempt_id,
                                                double now_ms)
{
  ChunkRenderDemandRecord &rec = GetOrCreate(coord);
  if (attempt_id != 0)
  {
    rec.active_attempt_id = attempt_id;
  }
  else if (!rec.has_active_attempt)
  {
    rec.active_attempt_id = NextAttemptId_++;
  }
  rec.has_active_attempt = true;
  rec.active_stage = stage;
  if (now_ms > 0.0)
  {
    rec.last_progress_ms = now_ms;
  }
}

void UChunkRenderDemandStore::NoteInstallResult(glm::ivec3 coord,
                                               InstallResult result,
                                               uint64_t published_geom_rev,
                                               uint64_t published_light_rev)
{
  ChunkRenderDemandRecord &rec = GetOrCreate(coord);
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
  const uint8_t newly =
      static_cast<uint8_t>(face_mask &
                           static_cast<uint8_t>(~rec.face_debt_mask));
  rec.face_debt_mask =
      static_cast<uint8_t>(rec.face_debt_mask | face_mask);
  if (peer_gen != 0)
  {
    for (int f = 0; f < 6; ++f)
    {
      if ((newly & static_cast<uint8_t>(1u << f)) != 0)
      {
        rec.waiting_peer_gen[f] = peer_gen;
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
    if (peer_gen != 0 && rec->waiting_peer_gen[f] != 0 &&
        rec->waiting_peer_gen[f] != peer_gen)
    {
      continue; // stale peer commit — keep debt
    }
    clear_mask = static_cast<uint8_t>(clear_mask | bit);
    rec->waiting_peer_gen[f] = 0;
  }
  rec->face_debt_mask =
      static_cast<uint8_t>(rec->face_debt_mask &
                           static_cast<uint8_t>(~clear_mask));
}

UChunkRenderDemandStore::ReconcileStats
UChunkRenderDemandStore::ReconcileMaintenance(int max_n)
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
    const ChunkRenderDemandRecord &rec = it->second;
    ++stats.checked;
    if (rec.desired_geom_rev != rec.published_geom_rev ||
        rec.desired_light_rev != rec.published_light_rev)
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
      ++stats.orphan_active;
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

void UChunkRenderDemandStore::Clear()
{
  Records_.clear();
  ReconcileCursor_ = 0;
}

} // namespace cutum
