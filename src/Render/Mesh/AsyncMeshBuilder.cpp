#include "Render/Mesh/AsyncMeshBuilder.h"
#include "Blocks/BlockCatalogQueries.h"
#include "Blocks/BlockRegistry.h"
#include "Core/Jobs/JobThreadBudget.h"
#include "Core/Jobs/PipelineAdmission.h"
#include "Render/Mesh/CrossInstanceCollector.h"
#include "Render/Mesh/GreedyMeshEmitter.h"
#include "Render/Mesh/GreedyMesher.h"
#include "Render/Mesh/IUChunkMesher.h"
#include "Render/Mesh/MeshLightSampling.h"
#include "World/Core/RuntimeTuning.h"
#include "World/Diagnostics/JobStageTrace.h"
#include "World/Math/GridMath.h"
#include "World/Streaming/ChunkRenderDemand.h"
#include <algorithm>
#include <memory>
#include <unordered_set>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace cutum
{

namespace
{
std::size_t ResolveMeshWorkerCount(std::size_t thread_count)
{
  return ComputeWorkerThreadCount(JobPoolKind::MeshBuild, thread_count);
}

std::size_t EstimateMeshResultBytes(const MeshBuildResult &result)
{
  std::size_t bytes = sizeof(MeshBuildResult);
  for (const GreedyMeshBatch &batch : result.batches)
  {
    bytes += batch.vertices.capacity() * sizeof(GreedyMeshVertex);
    bytes += batch.indices.capacity() * sizeof(uint32_t);
  }
  if (result.PendingSnapshot)
  {
    bytes += sizeof(ChunkMeshSnapshot);
  }
  for (const auto &entry : result.crossCenters)
  {
    bytes += entry.second.capacity() * sizeof(CrossInstanceGpu);
  }
  return bytes + result.batches.capacity() * sizeof(GreedyMeshBatch);
}
} // namespace

UAsyncMeshBuilder::UAsyncMeshBuilder(std::size_t thread_count)
    : WorkerCount(static_cast<int>(ResolveMeshWorkerCount(thread_count))),
      Pool(ResolveMeshWorkerCount(thread_count), "MeshBuild")
{
  const int slots = URuntimeTuning::Get().MeshCompletedSlots;
  const std::size_t cap =
      slots > 0 ? static_cast<std::size_t>(slots)
                : static_cast<std::size_t>(WorkerCount * kPipelineSlotsPerWorker);
  Completed.SetCapacity(cap);
}

namespace
{
constexpr int kCrossScanBelow = 2;
constexpr int kCrossScanAbove = 4;

int MaxSolidLocalYSnapshot(const ChunkMeshSnapshot &snapshot,
                           const UBlockRegistry &registry)
{
  int max_y = 0;
  for (int ly = 0; ly < CHUNK_SIZE; ++ly)
  {
    for (int lz = 0; lz < CHUNK_SIZE; ++lz)
    {
      for (int lx = 0; lx < CHUNK_SIZE; ++lx)
      {
        const BlockId id = snapshot.GetBlockLocal(glm::ivec3(lx, ly, lz));
        if (id == BLOCK_AIR ||
            registry.GetRenderStyle(id) == BlockRenderStyle::Cross)
        {
          continue;
        }
        max_y = std::max(max_y, ly);
      }
    }
  }
  return max_y;
}

} // namespace

bool UAsyncMeshBuilder::Enqueue(ChunkMeshSnapshot snapshot,
                                UBlockRegistry &registry, bool force_cpu)
{
  return EnqueueDetailed(std::move(snapshot), registry, force_cpu) ==
         MeshEnqueueResult::Accepted;
}

MeshEnqueueResult
UAsyncMeshBuilder::EnqueueDetailed(ChunkMeshSnapshot snapshot,
                                   UBlockRegistry &registry, bool force_cpu)
{
  const glm::ivec3 coord = snapshot.coord;
  // Audit R12: shared mesh/relight/gen concurrency envelope.
  if (!UPipelineAdmission::Get().TryAcquireWorkSlot())
  {
    return MeshEnqueueResult::WorkSlotRejected;
  }
  struct WorkSlotGuard
  {
    ~WorkSlotGuard() { UPipelineAdmission::Get().ReleaseWorkSlot(); }
  };
  auto work_slot = std::make_shared<WorkSlotGuard>();
  // Q7: snapshot credit reserved before worker enqueue (JobAdmissionLifetimeTest).
  if (!UPipelineAdmission::Get().TryAcquireSnapshotBytes(
          sizeof(ChunkMeshSnapshot)))
  {
    return MeshEnqueueResult::SnapshotBudgetRejected;
  }
  // std::function is copyable; the last queued/running closure owns the lease.
  auto snapshot_credit = std::make_shared<UPipelineCreditGuard>(
      PipelineCreditKind::Snapshot, sizeof(ChunkMeshSnapshot), true);
  const uint64_t submitEpoch = Epoch.load(std::memory_order_acquire);
  const uint64_t jobId =
      NextJobId.fetch_add(1, std::memory_order_relaxed);
  JobStageSpan stage_trace{};
  stage_trace.cx = coord.x;
  stage_trace.cy = coord.y;
  stage_trace.cz = coord.z;
  stage_trace.job_id = jobId;
  stage_trace.created_ms = std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now()
                                   .time_since_epoch())
                               .count();
  stage_trace.source_geom_rev = snapshot.sourceRevision;
  stage_trace.source_light_rev =
      snapshot.inputStampsValid ? snapshot.inputStamps[0].light : 0;
  if (snapshot.inputStampsValid)
  {
    stage_trace.incarnation = snapshot.inputStamps[0].incarnation;
  }
  if (const ChunkRenderDemandRecord *demand =
          RenderDemandStore ? RenderDemandStore->Find(coord) : nullptr)
  {
    stage_trace.attempt_id = demand->active_attempt_id;
    (void)StampChunkRenderDemandTrace(stage_trace, *RenderDemandStore, coord);
  }
  const auto retry_allowed = std::make_shared<std::atomic<bool>>(true);
  // Keep the definitions catalog alive for the whole job even if Reload swaps
  // Active mid-flight. Registry maps are separately mutex-protected.
  auto catalogKeep = registry.GetDefinitionsCatalogSnapshot();
  // The queued closure may start immediately. Publish its tentative owner while
  // holding the same lock through pool admission; a refusal restores the exact
  // previous owner without revoking that accepted job's retry permission.
  std::unique_lock<std::mutex> submission_lock(InFlightMutex);
  const auto previous_it = InFlight.find(coord);
  const bool had_previous = previous_it != InFlight.end();
  const TrackedJob previous = had_previous ? previous_it->second : TrackedJob{};
  InFlight[coord] = {jobId, retry_allowed};

  if (!Pool.TryEnqueue(
          [this, snapshot = std::move(snapshot), registryPtr = &registry,
           catalogKeep = std::move(catalogKeep), jobId, submitEpoch, force_cpu,
           stage_trace, retry_allowed,
           snapshot_credit = std::move(snapshot_credit),
           work_slot = std::move(work_slot)]() mutable
          {
        (void)work_slot;
        const auto job_coord = snapshot.coord;
        const auto obsolete_locked = [&]() {
          if (Epoch.load(std::memory_order_acquire) != submitEpoch)
            return JobTerminalReason::SubmissionEpochChanged;
          const auto it = InFlight.find(job_coord);
          return it == InFlight.end() || it->second.id != jobId
                     ? JobTerminalReason::JobIdentityReplaced
                     : JobTerminalReason::None;
        };
        const auto reject_obsolete = [&]() {
          JobTerminalReason reason;
          {
            std::lock_guard<std::mutex> lock(InFlightMutex);
            reason = obsolete_locked();
          }
          if (reason == JobTerminalReason::None) return false;
          NoteObsoleteJob(job_coord, stage_trace, reason, retry_allowed);
          return true;
        };
        if (reject_obsolete()) return;
        const auto worker_started = std::chrono::steady_clock::now();
        stage_trace.stage = JobStage::Started;
        stage_trace.elapsed_ms = std::chrono::duration<double, std::milli>(
                                     worker_started.time_since_epoch())
                                     .count() -
                                 stage_trace.created_ms;
        UJobStageTrace::Note(stage_trace);
        MeshBuildResult result;
        result.coord = snapshot.coord;
        result.sourceRevision = snapshot.sourceRevision;
        result.sourceWorldEpoch = snapshot.sourceWorldEpoch;
        result.sourceCaptureGeneration = snapshot.sourceCaptureGeneration;
        result.lightInputs = snapshot.lightInputs;
        result.jobId = jobId;
        result.submitEpoch = submitEpoch;
        result.RetryAllowed = retry_allowed;
        result.InputStamps = snapshot.inputStamps;
        result.InputLightHaloSignatures = snapshot.lightHaloSignatures;
        result.CertifiedPaddedLight = snapshot.certifiedPaddedLight;
        result.InputStampsValid = snapshot.inputStampsValid;
        result.InputCatalog = catalogKeep;
        result.GeometryInputs = snapshot.GeometryRenderInputs(catalogKeep);
        result.RenderLight = snapshot.renderLight;
        result.BoundaryOverlay = snapshot.boundaryOverlay;
        result.ProvisionalLightPreview = snapshot.provisionalLightPreview;
        const BlockDefinitionCatalog *pinned = catalogKeep.get();

        auto *gpu_mesher = Mesher;
        // Q4: WorkerCompute GPU-extract eligibility from pinned catalog.
        const bool defer_gpu =
            !force_cpu && gpu_mesher &&
            gpu_mesher->CanDeferGpuExtract(snapshot, *registryPtr, pinned);
        if (defer_gpu)
        {
          // GPF1: defer opaque-solid chunks to main-thread GPU emit.
          result.GpuExtractPending = true;
          result.PendingSnapshot =
              std::make_unique<ChunkMeshSnapshot>(std::move(snapshot));
          CollectCrossInstancesFromSnapshot(*result.PendingSnapshot, *registryPtr,
                                            pinned, result.crossCenters);
        }
        else
        {
          std::unordered_map<BlockId, GreedyMeshBatch> byBlockId;
          // Q4: WorkerCompute uses pinned catalog for faces/liquid/movement/cross
          // and GPU-extract eligibility (above).
          const auto quads =
              !force_cpu && Mesher
                  ? Mesher->BuildChunkMesh(snapshot, *registryPtr, pinned)
                  : UGreedyMesher::BuildChunkMesh(snapshot, *registryPtr, pinned);
          for (const GreedyQuad &q : quads)
          {
            GreedyMeshBatch &batch = byBlockId[q.Id];
            batch.blockId = q.Id;
            batch.Transparent = CatalogIsTransparent(pinned, q.Id);
            batch.AlphaCutout =
                CatalogGetRenderStyle(pinned, q.Id) == BlockRenderStyle::Cutout;
            const size_t base_vertex = batch.vertices.size();
            AppendGreedyQuad(q, snapshot.coord, batch.vertices, batch.indices);
            for (size_t i = base_vertex; i < batch.vertices.size(); ++i)
            {
              ApplyVertexLight(batch.vertices[i], q.LightPacked);
              if (result.ProvisionalLightPreview)
              {
                batch.vertices[i].lightPreview = 1.0f;
              }
            }
          }
          CollectCrossInstancesFromSnapshot(snapshot, *registryPtr, pinned,
                                            result.crossCenters);
          result.batches.reserve(byBlockId.size());
          for (auto &entry : byBlockId)
          {
            entry.second.blockId = entry.first;
            result.batches.push_back(std::move(entry.second));
          }
        }
        stage_trace.stage = JobStage::Built;
        stage_trace.stage_ms = std::chrono::duration<double, std::milli>(
                                   std::chrono::steady_clock::now() -
                                   worker_started)
                                   .count();
        stage_trace.elapsed_ms =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count() -
            stage_trace.created_ms;
        result.stageTrace = stage_trace;
        UJobStageTrace::Note(stage_trace);
        const std::size_t result_bytes = EstimateMeshResultBytes(result);
        if (reject_obsolete()) return;
        auto &pipe_adm = UPipelineAdmission::Get();
        if (!pipe_adm.TryAcquireResultBytes(result_bytes))
        {
          UJobStageTrace::NoteTerminal(
              result.stageTrace, JobStage::Retired,
              JobTerminalReason::ResultMemoryBudgetRejected);
          std::lock_guard<std::mutex> owner_lock(InFlightMutex);
          const auto it = InFlight.find(result.coord);
          if (retry_allowed->load(std::memory_order_relaxed) &&
              (it == InFlight.end() || it->second.id == jobId))
          {
            if (it != InFlight.end())
              InFlight.erase(it);
            std::lock_guard<std::mutex> overflow_lock(OverflowMutex);
            OverflowCoords.push_back(result.coord);
          }
          return;
        }
        result.ResultCredit = std::make_unique<UPipelineCreditGuard>(
            PipelineCreditKind::Result, result_bytes, true);

        MeshBuildResult dropped;
        bool overflow = false;
        JobTerminalReason obsolete;
        {
          // Cancel and completion publication share ownership locking. A late
          // cancelled result cannot displace a protected completed job.
          std::lock_guard<std::mutex> lock(InFlightMutex);
          obsolete = obsolete_locked();
          if (obsolete == JobTerminalReason::None)
            overflow = Completed.PushDropOldest(std::move(result), &dropped);
        }
        if (obsolete != JobTerminalReason::None)
        {
          NoteObsoleteJob(job_coord, stage_trace, obsolete, retry_allowed);
          return;
        }
        if (overflow)
        {
          UJobStageTrace::NoteTerminal(
              dropped.stageTrace, JobStage::Retired,
              JobTerminalReason::CompletedQueueOverflow);
          std::lock_guard<std::mutex> owner_lock(InFlightMutex);
          const auto it = InFlight.find(dropped.coord);
          if ((!dropped.RetryAllowed ||
         dropped.RetryAllowed->load(std::memory_order_relaxed)) &&
        (it == InFlight.end() || it->second.id == dropped.jobId))
          {
            if (it != InFlight.end())
              InFlight.erase(it);
            std::lock_guard<std::mutex> overflow_lock(OverflowMutex);
            OverflowCoords.push_back(dropped.coord);
          }
        }
          }, jobId))
  {
    if (had_previous)
      InFlight[coord] = previous;
    else
      InFlight.erase(coord);
    submission_lock.unlock();
    UJobStageTrace::NoteTerminal(stage_trace, JobStage::Cancelled,
                                 JobTerminalReason::WorkerPoolRejected);
    return MeshEnqueueResult::WorkerPoolRejected;
  }
  if (had_previous)
    previous.retry_allowed->store(false, std::memory_order_relaxed);
  submission_lock.unlock();
  return MeshEnqueueResult::Accepted;
}

void UAsyncMeshBuilder::SetCompletedCapacity(std::size_t cap)
{
  for (auto &dropped : Completed.SetCapacity(cap))
  {
    UJobStageTrace::NoteTerminal(
        dropped.stageTrace, JobStage::Retired,
        JobTerminalReason::CompletedQueueCapacityReduced);
    std::lock_guard<std::mutex> owner_lock(InFlightMutex);
    const auto it = InFlight.find(dropped.coord);
    // Shrinking the queue retires the victim, not a newer job with its key.
    if ((!dropped.RetryAllowed ||
         dropped.RetryAllowed->load(std::memory_order_relaxed)) &&
        (it == InFlight.end() || it->second.id == dropped.jobId))
    {
      if (it != InFlight.end())
        InFlight.erase(it);
      std::lock_guard<std::mutex> overflow_lock(OverflowMutex);
      OverflowCoords.push_back(dropped.coord);
    }
  }
}

std::vector<MeshBuildResult> UAsyncMeshBuilder::DrainCompleted(int maxPerFrame)
{
  const std::size_t take =
      maxPerFrame <= 0 ? 0 : static_cast<std::size_t>(maxPerFrame);
  std::vector<MeshBuildResult> drained = Completed.DrainUpTo(take);
  const uint64_t current_epoch = Epoch.load(std::memory_order_acquire);
  std::vector<MeshBuildResult> accepted;
  accepted.reserve(drained.size());

  struct TerminalEvent
  {
    JobStageSpan span{};
    JobTerminalReason reason{JobTerminalReason::None};
  };
  std::vector<TerminalEvent> terminal_events;
  terminal_events.reserve(drained.size());
  std::vector<glm::ivec3> discarded_now;
  discarded_now.reserve(drained.size());
  {
    std::lock_guard<std::mutex> lock(InFlightMutex);
    for (MeshBuildResult &result : drained)
    {
      if (result.submitEpoch != current_epoch)
      {
        DiscardedLate.fetch_add(1, std::memory_order_relaxed);
        DiscardedLateEpoch.fetch_add(1, std::memory_order_relaxed);
        const auto it = InFlight.find(result.coord);
        const bool newer_owned =
            it != InFlight.end() && it->second.id != result.jobId;
        if (it != InFlight.end() && it->second.id == result.jobId)
        {
          InFlight.erase(it);
        }
        if (!newer_owned &&
            (!result.RetryAllowed ||
             result.RetryAllowed->load(std::memory_order_relaxed)))
          discarded_now.push_back(result.coord);
        terminal_events.push_back(
            {result.stageTrace, JobTerminalReason::SubmissionEpochChanged});
        continue;
      }
      const auto it = InFlight.find(result.coord);
      if (it == InFlight.end() || it->second.id != result.jobId)
      {
        DiscardedLate.fetch_add(1, std::memory_order_relaxed);
        DiscardedLateJobMismatch.fetch_add(1, std::memory_order_relaxed);
        // A replacement already owns this obligation. Retiring its predecessor
        // must not create a second executable request for the same coordinate.
        if (it == InFlight.end() &&
            (!result.RetryAllowed ||
             result.RetryAllowed->load(std::memory_order_relaxed)))
          discarded_now.push_back(result.coord);
        terminal_events.push_back(
            {result.stageTrace, JobTerminalReason::JobIdentityReplaced});
        continue;
      }
      InFlight.erase(it);
      accepted.push_back(std::move(result));
    }
  }
  for (const TerminalEvent &event : terminal_events)
  {
    UJobStageTrace::NoteTerminal(event.span, JobStage::Retired,
                                 event.reason);
  }
  if (!discarded_now.empty())
  {
    std::lock_guard<std::mutex> dlock(DiscardedMutex);
    DiscardedCoords.insert(DiscardedCoords.end(), discarded_now.begin(),
                           discarded_now.end());
  }
  return accepted;
}

bool UAsyncMeshBuilder::IsInFlight(glm::ivec3 coord) const
{
  std::lock_guard<std::mutex> lock(InFlightMutex);
  return InFlight.find(coord) != InFlight.end();
}

int UAsyncMeshBuilder::GetInFlightCount() const
{
  int tracked = 0;
  {
    std::lock_guard<std::mutex> lock(InFlightMutex);
    tracked = static_cast<int>(InFlight.size());
  }
  // ForgetInflight erases tracking while the worker is still Active/Pending —
  // schedule must count real pool depth or Completed fills with vertex RAM.
  const int pool = static_cast<int>(Pool.GetActiveJobCount() +
                                    Pool.GetPendingJobCount());
  return std::max(tracked, pool);
}

bool UAsyncMeshBuilder::HasPendingWork() const
{
  if (!Completed.Empty())
  {
    return true;
  }
  std::lock_guard<std::mutex> lock(InFlightMutex);
  return !InFlight.empty();
}

bool UAsyncMeshBuilder::HasInflightInHorizontalRadius(
    glm::ivec3 center_ground_chunk, int radius_chunks) const
{
  if (radius_chunks < 0)
  {
    return false;
  }
  // Era53 / Enter SoT: only near Completed blocks spawn ring — far pool depth
  // must not sticky-block IsSpawnMeshRingReady / mesh warmup forever.
  if (Completed.Any(
          [&](const MeshBuildResult &result)
          {
            const int horiz =
                std::max(std::abs(result.coord.x - center_ground_chunk.x),
                         std::abs(result.coord.z - center_ground_chunk.z));
            return horiz <= radius_chunks;
          }))
  {
    return true;
  }
  std::lock_guard<std::mutex> lock(InFlightMutex);
  for (const auto &kv : InFlight)
  {
    const glm::ivec3 &coord = kv.first;
    const int horiz = std::max(std::abs(coord.x - center_ground_chunk.x),
                               std::abs(coord.z - center_ground_chunk.z));
    if (horiz <= radius_chunks)
    {
      return true;
    }
  }
  return false;
}

bool UAsyncMeshBuilder::FindFirstUndrawableInflightInHorizontalBand(
    glm::ivec3 center_ground_chunk, int radius_chunks, int min_cy, int max_cy,
    const std::function<bool(glm::ivec3)> &has_drawable,
    glm::ivec3 &out_coord, bool &out_completed) const
{
  if (radius_chunks < 0 || min_cy > max_cy || !has_drawable)
  {
    return false;
  }
  const auto blocks = [&](glm::ivec3 coord)
  {
    const int horiz = std::max(std::abs(coord.x - center_ground_chunk.x),
                               std::abs(coord.z - center_ground_chunk.z));
    return horiz <= radius_chunks && coord.y >= min_cy && coord.y <= max_cy &&
           !has_drawable(coord);
  };
  // Completed work is first because it is ready to be drained/applied now.
  if (Completed.Any(
          [&](const MeshBuildResult &result)
          {
            if (!blocks(result.coord))
            {
              return false;
            }
            out_coord = result.coord;
            out_completed = true;
            return true;
          }))
  {
    return true;
  }
  std::lock_guard<std::mutex> lock(InFlightMutex);
  for (const auto &entry : InFlight)
  {
    if (!blocks(entry.first))
    {
      continue;
    }
    out_coord = entry.first;
    out_completed = false;
    return true;
  }
  return false;
}

void UAsyncMeshBuilder::WaitIdle() { Pool.WaitIdle(); }

bool UAsyncMeshBuilder::WaitIdleFor(const std::chrono::milliseconds timeout)
{
  return Pool.WaitIdleFor(timeout);
}

void UAsyncMeshBuilder::CancelPending()
{
  {
    std::lock_guard<std::mutex> lock(InFlightMutex);
    Epoch.fetch_add(1, std::memory_order_acq_rel);
    // Cancellation retires these jobs. KeepDirty transfers their obligations
    // at the cache owner before calling us; late workers must not recreate them.
    for (const auto &entry : InFlight)
      entry.second.retry_allowed->store(false, std::memory_order_relaxed);
    InFlight.clear();
    std::lock_guard<std::mutex> overflow_lock(OverflowMutex);
    OverflowCoords.clear();
    std::lock_guard<std::mutex> discarded_lock(DiscardedMutex);
    DiscardedCoords.clear();
  }
  Pool.CancelPendingJobs();
  const uint64_t current_epoch = Epoch.load(std::memory_order_acquire);
  for (MeshBuildResult &result : Completed.DrainAll())
  {
    if (result.submitEpoch != current_epoch)
    {
      DiscardedLate.fetch_add(1, std::memory_order_relaxed);
      DiscardedLateEpoch.fetch_add(1, std::memory_order_relaxed);
    }
  }
}

void UAsyncMeshBuilder::ForgetInflight(const glm::ivec3 coord)
{
  uint64_t retired_id = 0;
  {
    std::lock_guard<std::mutex> lock(InFlightMutex);
    const auto current = InFlight.find(coord);
    if (current != InFlight.end())
    {
      retired_id = current->second.id;
      current->second.retry_allowed->store(false, std::memory_order_relaxed);
      InFlight.erase(current);
    }
    // Overflow may have retired the job before its owner abandons the coord.
    // Its already-produced retry must be retired even without an InFlight entry.
    std::lock_guard<std::mutex> overflow_lock(OverflowMutex);
    OverflowCoords.erase(std::remove(OverflowCoords.begin(), OverflowCoords.end(), coord),
                         OverflowCoords.end());
    std::lock_guard<std::mutex> discarded_lock(DiscardedMutex);
    DiscardedCoords.erase(std::remove(DiscardedCoords.begin(), DiscardedCoords.end(), coord),
                          DiscardedCoords.end());
  }
  if (retired_id == 0) return;
  Pool.CancelPendingJobsIf([&](uint64_t id) { return id == retired_id; });
  Completed.EraseIf([&](const MeshBuildResult &result) {
    if (result.jobId != retired_id) return false;
    UJobStageTrace::NoteTerminal(result.stageTrace, JobStage::Retired,
                                JobTerminalReason::JobIdentityReplaced);
    return true;
  });
}

void UAsyncMeshBuilder::NoteObsoleteJob(glm::ivec3 coord,
                                       const JobStageSpan &span,
                                       JobTerminalReason reason,
                                       const std::shared_ptr<std::atomic<bool>> &retry_allowed)
{
  DiscardedLate.fetch_add(1, std::memory_order_relaxed);
  if (reason == JobTerminalReason::SubmissionEpochChanged)
    DiscardedLateEpoch.fetch_add(1, std::memory_order_relaxed);
  else
    DiscardedLateJobMismatch.fetch_add(1, std::memory_order_relaxed);
  {
    std::lock_guard<std::mutex> owner_lock(InFlightMutex);
    const auto current = InFlight.find(coord);
    if (retry_allowed->load(std::memory_order_relaxed) &&
        (current == InFlight.end() || current->second.id == span.job_id))
    {
      std::lock_guard<std::mutex> debt_lock(DiscardedMutex);
      DiscardedCoords.push_back(coord);
    }
  }
  UJobStageTrace::NoteTerminal(span, JobStage::Retired, reason);
}

void UAsyncMeshBuilder::CancelPendingOutsideHorizontalRadius(glm::ivec3 focus,
                                                            int keep_radius)
{
  if (keep_radius < 0) { CancelPending(); return; }
  std::unordered_set<uint64_t> protected_jobs;
  {
    std::lock_guard<std::mutex> lock(InFlightMutex);
    for (auto it = InFlight.begin(); it != InFlight.end();)
    {
      const int horizontal = std::max(std::abs(it->first.x-focus.x),
                                       std::abs(it->first.z-focus.z));
      if (horizontal <= keep_radius)
      {
        protected_jobs.insert(it->second.id);
        ++it;
      }
      else
      {
        it->second.retry_allowed->store(false, std::memory_order_relaxed);
        it = InFlight.erase(it);
      }
    }
    const auto outside = [&](glm::ivec3 coord) {
      return std::max(std::abs(coord.x - focus.x),
                      std::abs(coord.z - focus.z)) > keep_radius;
    };
    // Already-produced retries have the same cancellation boundary as jobs.
    // Protected retries remain owned even when overflow retired their CPU job.
    std::lock_guard<std::mutex> overflow_lock(OverflowMutex);
    OverflowCoords.erase(std::remove_if(OverflowCoords.begin(), OverflowCoords.end(), outside),
                         OverflowCoords.end());
    std::lock_guard<std::mutex> discarded_lock(DiscardedMutex);
    DiscardedCoords.erase(std::remove_if(DiscardedCoords.begin(), DiscardedCoords.end(), outside),
                          DiscardedCoords.end());
  }
  Pool.CancelPendingJobsIf([&](uint64_t id) { return !protected_jobs.count(id); });
  Completed.EraseIf([&](const MeshBuildResult &result) {
    if (protected_jobs.count(result.jobId)) return false;
    UJobStageTrace::NoteTerminal(result.stageTrace, JobStage::Cancelled,
                                 JobTerminalReason::JobIdentityReplaced);
    return true;
  });
}

std::vector<glm::ivec3> UAsyncMeshBuilder::TakeOverflowCoords()
{
  std::lock_guard<std::mutex> olock(OverflowMutex);
  std::vector<glm::ivec3> out;
  out.swap(OverflowCoords);
  return out;
}

std::vector<glm::ivec3> UAsyncMeshBuilder::TakeDiscardedCoords()
{
  std::lock_guard<std::mutex> dlock(DiscardedMutex);
  std::vector<glm::ivec3> out;
  out.swap(DiscardedCoords);
  return out;
}

} // namespace cutum
