#include "World/Streaming/ColumnRecordCoordinator.h"

#include "glog/logging.h"

#include <chrono>

namespace cutum
{

namespace
{
uint64_t NowMs()
{
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

ColumnJobStage PendingStageFromTruth(const ColumnWorldTruth &truth)
{
  if (truth.pending_light)
  {
    return ColumnJobStage::PendingLight;
  }
  if (truth.meshing)
  {
    return ColumnJobStage::Meshing;
  }
  if (truth.gpu_pending)
  {
    return ColumnJobStage::GpuPending;
  }
  return ColumnJobStage::Absent;
}

void TouchDebtProgress(ColumnRecord &rec)
{
  const uint64_t now = NowMs();
  if (rec.debt.created_at_ms == 0)
  {
    rec.debt.created_at_ms = now;
  }
  rec.debt.last_progress_at_ms = now;
}

void ClearPending(ColumnRecord &rec)
{
  rec.pending.token = 0;
  rec.pending.stage = ColumnJobStage::Absent;
  rec.pending.deps = 0;
  rec.pending.priority = ColumnJobPriority::Background;
}

} // namespace

ColumnJobStage UColumnRecordCoordinator::SyncFromWorldTruth(
    ColumnRecord &rec, const ColumnWorldTruth &truth)
{
  rec.resident = truth.has_chunk;
  if (!truth.has_chunk)
  {
    rec.published = {};
    ClearPending(rec);
    rec.debt = {};
    return ColumnJobStage::Absent;
  }

  if (truth.render_ready)
  {
    rec.published.mesh_version = std::max(rec.published.mesh_version, rec.mesh_rev);
    if (rec.published.gpu_handle == 0)
    {
      rec.published.gpu_handle = 1;
    }
    rec.published.bounds_version =
        std::max(rec.published.bounds_version, rec.content_rev);
  }

  const ColumnJobStage pending_stage = PendingStageFromTruth(truth);
  if (pending_stage != ColumnJobStage::Absent)
  {
    if (rec.pending.token == 0)
    {
      rec.pending.token = rec.inflight_job != 0 ? rec.inflight_job : 1;
      rec.debt.created_at_ms = NowMs();
    }
    rec.pending.stage = pending_stage;
    TouchDebtProgress(rec);
  }
  else if (rec.inflight_job == 0)
  {
    ClearPending(rec);
  }

  rec.pending_light = truth.pending_light;
  return DeriveJobStageFromRecord(rec);
}

ColumnJobStage UColumnRecordCoordinator::DeriveJobStageFromRecord(
    const ColumnRecord &rec)
{
  if (!rec.resident)
  {
    return ColumnJobStage::Absent;
  }
  if (ColumnHasActivePending(rec) &&
      rec.pending.stage != ColumnJobStage::Absent &&
      rec.pending.stage != ColumnJobStage::RenderReady)
  {
    return rec.pending.stage;
  }
  if (rec.pending_light)
  {
    return ColumnJobStage::PendingLight;
  }
  if (ColumnHasPublishedRender(rec))
  {
    return ColumnJobStage::RenderReady;
  }
  return ColumnJobStage::Gen;
}

void UColumnRecordCoordinator::LogShadowMismatch(glm::ivec2 column,
                                               ColumnJobStage legacy_stage,
                                               ColumnJobStage record_stage)
{
  if (legacy_stage == record_stage)
  {
    return;
  }
  VLOG(1) << "ColumnRecord shadow mismatch col=(" << column.x << "," << column.y
          << ") legacy=" << ColumnJobStageName(legacy_stage) << " record="
          << ColumnJobStageName(record_stage);
}

} // namespace cutum
