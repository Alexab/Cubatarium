#include "World/Streaming/ColumnRecordCoordinator.h"

#include "glog/logging.h"

#include <chrono>

namespace cutum
{

namespace
{
std::atomic<uint8_t> g_cutover_stage{
    static_cast<uint8_t>(ColumnCutoverStage::SeamOwner)};
std::atomic<uint64_t> g_shadow_mismatch_n{0};
std::atomic<int> g_shadow_stage_disagree_focus_n{0};

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

ColumnCutoverStage UColumnRecordCoordinator::GetCutoverStage()
{
  return static_cast<ColumnCutoverStage>(
      g_cutover_stage.load(std::memory_order_relaxed));
}

void UColumnRecordCoordinator::SetCutoverStage(ColumnCutoverStage stage)
{
  g_cutover_stage.store(static_cast<uint8_t>(stage), std::memory_order_relaxed);
}

uint64_t UColumnRecordCoordinator::ShadowMismatchCount()
{
  return g_shadow_mismatch_n.load(std::memory_order_relaxed);
}

void UColumnRecordCoordinator::ResetShadowMismatchCount()
{
  g_shadow_mismatch_n.store(0, std::memory_order_relaxed);
}

int UColumnRecordCoordinator::ShadowStageDisagreeFocusN()
{
  return g_shadow_stage_disagree_focus_n.load(std::memory_order_relaxed);
}

void UColumnRecordCoordinator::SetShadowStageDisagreeFocusN(int n)
{
  g_shadow_stage_disagree_focus_n.store(n, std::memory_order_relaxed);
}

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

  // Authoritative GPU/publication owner remains legacy maps until Q6 cutover.
  if (truth.render_ready)
  {
    rec.published.mesh_version = std::max(rec.published.mesh_version, rec.mesh_rev);
    if (truth.published_gpu_handle != 0)
    {
      rec.published.gpu_handle = truth.published_gpu_handle;
      rec.published.shadow_synthetic = false;
    }
    else
    {
      rec.published.gpu_handle = 0;
      rec.published.shadow_synthetic = true;
    }
    rec.published.bounds_version =
        std::max(rec.published.bounds_version, rec.content_rev);
    // Useful progress: published drawable advances debt clock.
    TouchDebtProgress(rec);
  }

  const ColumnJobStage pending_stage = PendingStageFromTruth(truth);
  if (pending_stage != ColumnJobStage::Absent)
  {
    const bool progressed = rec.pending.token == 0 ||
                            rec.pending.stage != pending_stage;
    if (rec.pending.token == 0 && rec.inflight_job != 0)
    {
      rec.pending.token = rec.inflight_job;
      rec.debt.created_at_ms = NowMs();
    }
    rec.pending.stage = pending_stage;
    if (progressed) TouchDebtProgress(rec);
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

bool UColumnRecordCoordinator::RecordWantsFirstMeshEnqueue(
    const ColumnRecord &rec)
{
  // Keep-until-replace only: refuse while Meshing/GpuPending is already tracked.
  // RenderReady may still want FirstMesh (SoftDefer empty / hole repair on a
  // published predecessor) — matches legacy SLA and FirstMeshOwner cutover.
  const ColumnJobStage stage = DeriveJobStageFromRecord(rec);
  return stage != ColumnJobStage::Meshing &&
         stage != ColumnJobStage::GpuPending;
}

bool UColumnRecordCoordinator::RecordWantsRelightEnqueue(
    const ColumnRecord &rec)
{
  // Refuse while PendingLight already in flight; RenderReady may still relight.
  const ColumnJobStage stage = DeriveJobStageFromRecord(rec);
  return stage != ColumnJobStage::PendingLight;
}

bool UColumnRecordCoordinator::RecordWantsSeamEnqueue(const ColumnRecord &rec)
{
  const ColumnJobStage stage = DeriveJobStageFromRecord(rec);
  return stage != ColumnJobStage::Meshing &&
         stage != ColumnJobStage::GpuPending;
}

bool UColumnRecordCoordinator::RecordWantsEvict(const ColumnRecord &rec)
{
  return !ColumnHasActivePending(rec);
}

void UColumnRecordCoordinator::LogShadowMismatch(glm::ivec2 column,
                                               ColumnJobStage legacy_stage,
                                               ColumnJobStage record_stage)
{
  if (legacy_stage == record_stage)
  {
    return;
  }
  // Decide* enqueue/evict parity only — Sync stage diffs use the focus gauge.
  g_shadow_mismatch_n.fetch_add(1, std::memory_order_relaxed);
  VLOG(1) << "ColumnRecord decide shadow mismatch col=(" << column.x << ","
          << column.y << ") legacy=" << ColumnJobStageName(legacy_stage)
          << " record=" << ColumnJobStageName(record_stage);
}

bool UColumnRecordCoordinator::DecideFirstMeshEnqueue(bool legacy_want,
                                                      bool record_want,
                                                      glm::ivec2 column,
                                                      bool count_mismatch)
{
  const ColumnCutoverStage stage = GetCutoverStage();
  if (stage >= ColumnCutoverStage::FirstMeshOwner)
  {
    return record_want;
  }
  // ShadowCompare: never dual-enqueue; legacy owns; log parity gaps.
  if (count_mismatch && legacy_want != record_want)
  {
    LogShadowMismatch(column,
                      legacy_want ? ColumnJobStage::Meshing
                                  : ColumnJobStage::Absent,
                      record_want ? ColumnJobStage::Meshing
                                  : ColumnJobStage::Absent);
  }
  return legacy_want;
}

bool UColumnRecordCoordinator::DecideRelightEnqueue(bool legacy_want,
                                                    bool record_want,
                                                    glm::ivec2 column,
                                                    bool count_mismatch)
{
  const ColumnCutoverStage stage = GetCutoverStage();
  if (stage >= ColumnCutoverStage::RelightOwner)
  {
    return record_want;
  }
  if (count_mismatch && legacy_want != record_want)
  {
    LogShadowMismatch(column,
                      legacy_want ? ColumnJobStage::PendingLight
                                  : ColumnJobStage::Absent,
                      record_want ? ColumnJobStage::PendingLight
                                  : ColumnJobStage::Absent);
  }
  return legacy_want;
}

bool UColumnRecordCoordinator::DecideSeamEnqueue(bool legacy_want,
                                                 bool record_want,
                                                 glm::ivec2 column,
                                                 bool count_mismatch)
{
  const ColumnCutoverStage stage = GetCutoverStage();
  if (stage >= ColumnCutoverStage::SeamOwner)
  {
    return record_want;
  }
  if (count_mismatch && legacy_want != record_want)
  {
    LogShadowMismatch(column,
                      legacy_want ? ColumnJobStage::Meshing
                                  : ColumnJobStage::Absent,
                      record_want ? ColumnJobStage::Meshing
                                  : ColumnJobStage::Absent);
  }
  return legacy_want;
}

bool UColumnRecordCoordinator::DecideEvict(bool legacy_want, bool record_want,
                                           glm::ivec2 column,
                                           bool count_mismatch)
{
  const ColumnCutoverStage stage = GetCutoverStage();
  if (stage >= ColumnCutoverStage::EvictionOwner)
  {
    return record_want;
  }
  if (count_mismatch && legacy_want != record_want)
  {
    // Eviction has no ColumnJobStage twin; reuse Absent vs Gen as parity markers.
    LogShadowMismatch(column,
                      legacy_want ? ColumnJobStage::Gen : ColumnJobStage::Absent,
                      record_want ? ColumnJobStage::Gen : ColumnJobStage::Absent);
  }
  return legacy_want;
}

} // namespace cutum
