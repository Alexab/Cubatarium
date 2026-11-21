#include "World/Lighting/AsyncRelightBuilder.h"

#include "Blocks/BlockRegistry.h"
#include "Core/Environment.h"
#include "Core/Jobs/JobThreadBudget.h"
#include "Core/Jobs/PipelineAdmission.h"
#include "World/Core/BlockWorld.h"
#include "World/Core/RuntimeTuning.h"
#include "World/Streaming/DependencyStampBuilder.h"
#include "App/Platform/Log.h"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <thread>

namespace cutum
{

namespace
{
std::size_t ResolveRelightWorkerCount(std::size_t thread_count)
{
  return ComputeWorkerThreadCount(JobPoolKind::Relight, thread_count);
}

std::mutex gRelightCaptureMutex;
struct RelightWorkSlotGuard
{
  bool held{UPipelineAdmission::Get().TryAcquireWorkSlot()};
  ~RelightWorkSlotGuard()
  {
    if (held) UPipelineAdmission::Get().ReleaseWorkSlot();
  }
};
} // namespace

UAsyncRelightBuilder::UAsyncRelightBuilder(std::size_t thread_count)
    : WorkerCount(static_cast<int>(ResolveRelightWorkerCount(thread_count))),
      Pool(ResolveRelightWorkerCount(thread_count), "Relight")
{
  const int slots = URuntimeTuning::Get().RelightCompletedSlots;
  const std::size_t cap =
      slots > 0 ? static_cast<std::size_t>(slots)
                : static_cast<std::size_t>(WorkerCount * kPipelineSlotsPerWorker);
  Completed.SetCapacity(cap);
}

void UAsyncRelightBuilder::Enqueue(UChunkRelightSnapshot snapshot,
                                   const UBlockRegistry &registry)
{
  const uint64_t submit_epoch = Epoch.load(std::memory_order_acquire);
  const uint64_t job_id = snapshot.GetJobId() > 0
                              ? snapshot.GetJobId()
                              : NextJobId.fetch_add(1, std::memory_order_relaxed);
  auto work_slot = std::make_shared<RelightWorkSlotGuard>();
  if (!work_slot->held)
  {
    auto retry = snapshot.GetJobSpec();
    retry.job_id = job_id;
    std::lock_guard<std::mutex> lock(DiscardedSourcesMutex);
    DiscardedJobs.push_back(std::move(retry));
    return;
  }
  WorkToken work_token = snapshot.GetWorkToken();
  work_token.world_epoch = submit_epoch;
  work_token.domain = WorkDomain::Relight;
  work_token.generation = job_id;
  snapshot.SetSubmitContext(work_token, snapshot.GetDependencyStamp());
  {
    std::lock_guard<std::mutex> lock(InFlightMutex);
    InFlight[job_id] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    auto retry = snapshot.GetJobSpec();
    retry.job_id = job_id;
    RetryObligations[job_id] = {submit_epoch, std::move(retry)};
  }

  auto catalogKeep = registry.GetDefinitionsCatalogSnapshot();
  snapshot.SetInputCatalog(catalogKeep);

  if (!Pool.TryEnqueue([this, snapshot = std::move(snapshot), registryPtr = &registry,
                catalogKeep = std::move(catalogKeep), job_id,
                submit_epoch, work_slot = std::move(work_slot)]() mutable
               {
                 // Compute uses the pinned immutable material catalog throughout
                 // the flood; the registry is only a fallback for legacy callers.
                 RelightComputeResult result = snapshot.Compute(*registryPtr);
                 result.job_id = job_id;
                 result.submitEpoch = submit_epoch;
                 result.work_token = snapshot.GetWorkToken();
                 result.work_token.world_epoch = submit_epoch;
                 result.dependency_stamp = snapshot.GetDependencyStamp();
                 result.input_catalog = std::move(catalogKeep);
                 RelightComputeResult dropped;
                 if (Completed.PushDropOldest(std::move(result), &dropped))
                 {
                   NoteCompletedOverflow(std::move(dropped));
                 }
               }))
    ReturnUnsubmittedJob(job_id, submit_epoch);
}

AsyncRelightEnqueueTimings UAsyncRelightBuilder::EnqueueJob(
    const UBlockWorld &world, RelightJobSpec spec,
    const UBlockRegistry &registry)
{
  AsyncRelightEnqueueTimings timings;
  const bool audit_relight =
      IsEnvironmentFlagEnabled("CUBATARIUM_RELIGHT_AUDIT");
  const auto capture_started = std::chrono::steady_clock::now();
  const uint64_t submit_epoch = Epoch.load(std::memory_order_acquire);
  const uint64_t job_id =
      spec.job_id > 0 ? spec.job_id
                      : NextJobId.fetch_add(1, std::memory_order_relaxed);
  spec.job_id = job_id;

  // Refuse compute before allocating its captured payload. The normal World
  // discarded-job consumer retains/replays the complete request specification.
  auto work_slot = std::make_shared<RelightWorkSlotGuard>();
  if (!work_slot->held)
  {
    std::lock_guard<std::mutex> lock(DiscardedSourcesMutex);
    DiscardedJobs.push_back(std::move(spec));
    return timings;
  }

  UChunkRelightSnapshot snapshot;
  {
    const auto lock_wait_started = std::chrono::steady_clock::now();
    std::unique_lock<std::mutex> capture_lock(gRelightCaptureMutex);
    const auto lock_acquired = std::chrono::steady_clock::now();
    snapshot = UChunkRelightSnapshot::Capture(world, spec);
    const auto snapshot_captured = std::chrono::steady_clock::now();
    timings.capture_lock_wait_ms =
        std::chrono::duration<double, std::milli>(lock_acquired -
                                                  lock_wait_started)
            .count();
    timings.snapshot_copy_ms =
        std::chrono::duration<double, std::milli>(snapshot_captured -
                                                  lock_acquired)
            .count();
  }
  LastCaptureFullN = snapshot.GetCapturedFullChunks();
  LastCaptureNeighborLightN = snapshot.GetCapturedNeighborLightChunks();
  WorkToken work_token;
  work_token.world_epoch = submit_epoch;
  work_token.domain = WorkDomain::Relight;
  work_token.generation = job_id;
  DependencyStamp deps;
  const auto dependency_started = std::chrono::steady_clock::now();
  if (!spec.block_positions.empty())
  {
    const glm::ivec3 chunk =
        UChunkManager::WorldToChunk(spec.block_positions.front());
    work_token.coord = chunk;
    work_token.chunk_incarnation = ChunkIncarnationAt(world, chunk);
    deps = BuildRelightDependencyStamp(world, chunk, registry);
  }
  const auto dependency_complete = std::chrono::steady_clock::now();
  timings.dependency_stamp_ms =
      std::chrono::duration<double, std::milli>(dependency_complete -
                                                dependency_started)
          .count();
  snapshot.SetSubmitContext(work_token, deps);
  const double capture_ms = std::chrono::duration<double, std::milli>(
                                std::chrono::steady_clock::now() - capture_started)
                                .count();
  const int captured_full_n = snapshot.GetCapturedFullChunks();
  const int captured_neighbor_light_n =
      snapshot.GetCapturedNeighborLightChunks();
  const int captured_geometry_halo_n = snapshot.GetCapturedGeometryHaloChunks();
  const int captured_sky_context_n = snapshot.GetCapturedSkyContextChunks();
  const double sky_context_capture_ms = snapshot.GetSkyContextCaptureMs();
  if (audit_relight)
  {
    const glm::ivec3 source = spec.block_positions.empty()
                                  ? glm::ivec3(-1)
                                  : spec.block_positions.front();
    CubatariumLogInfo(
        "RelightAudit",
        "snapshot job=" + std::to_string(job_id) + " source=(" +
            std::to_string(source.x) + "," + std::to_string(source.y) + "," +
            std::to_string(source.z) +
            ") band=" + std::to_string(spec.min_world_y) + ":" +
            std::to_string(spec.max_world_y) +
            " capture_ms=" + std::to_string(capture_ms) +
            " full=" + std::to_string(captured_full_n) +
            " neighbor_light=" + std::to_string(captured_neighbor_light_n) +
            " geometry_halo=" + std::to_string(captured_geometry_halo_n) +
            " sky_context=" + std::to_string(captured_sky_context_n) +
            " sky_context_ms=" + std::to_string(sky_context_capture_ms) +
            " finalize=" + std::to_string(spec.finalize_pending_gate) +
            " draw_gate=" + std::to_string(spec.visible_draw_gate_repair));
  }

  {
    std::lock_guard<std::mutex> lock(InFlightMutex);
    InFlight[job_id] = std::chrono::duration_cast<std::chrono::milliseconds>(
                           capture_started.time_since_epoch())
                           .count();
    RetryObligations[job_id] = {submit_epoch, spec};
  }

  auto catalogKeep = registry.GetDefinitionsCatalogSnapshot();
  snapshot.SetInputCatalog(catalogKeep);

  const auto submit_started = std::chrono::steady_clock::now();
  if (!Pool.TryEnqueue(
      [this, snapshot = std::move(snapshot), registry = &registry,
       catalogKeep = std::move(catalogKeep), job_id, submit_epoch,
       work_slot = std::move(work_slot), audit_relight, captured_full_n,
       captured_neighbor_light_n, captured_geometry_halo_n,
       captured_sky_context_n, sky_context_capture_ms]() mutable
      {
        (void)work_slot;
        const auto compute_started = std::chrono::steady_clock::now();
        if (audit_relight)
        {
          CubatariumLogInfo("RelightAudit",
                            "worker_start job=" + std::to_string(job_id));
        }
        RelightComputeResult result = snapshot.Compute(*registry);
        const double compute_ms =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - compute_started)
                .count();
        if (audit_relight)
        {
          CubatariumLogInfo(
              "RelightAudit",
              "worker_done job=" + std::to_string(job_id) +
                  " compute_ms=" + std::to_string(compute_ms) + " full=" +
                  std::to_string(captured_full_n) + " neighbor_light=" +
                  std::to_string(captured_neighbor_light_n) +
                  " geometry_halo=" + std::to_string(captured_geometry_halo_n) +
                  " inputs_complete=" + std::to_string(result.inputs_complete) +
                  " unknown_inputs=" +
                  std::to_string(result.unknown_input_chunks) +
                  " sky_context=" + std::to_string(captured_sky_context_n) +
                  " sky_context_ms=" + std::to_string(sky_context_capture_ms) +
                  " chunks=" + std::to_string(result.chunks.size()) +
                  " read_set=" + std::to_string(result.read_set.size()) +
                  " frontier_unfinished=" +
                  std::to_string(result.frontier_unfinished) +
                  " sky_seed_candidates=" +
                  std::to_string(result.sky_flood_work.candidate_seeds) +
                  " sky_seeds=" +
                  std::to_string(result.sky_flood_work.queued_seeds) +
                  " sky_neighbor_visits=" +
                  std::to_string(result.sky_flood_work.neighbor_visits) +
                  " sky_saturated_tiles=" +
                  std::to_string(result.sky_flood_work.saturated_tiles) +
                  " block_seeds=" +
                  std::to_string(result.block_flood_work.queued_seeds) +
                  " block_neighbor_visits=" +
                  std::to_string(result.block_flood_work.neighbor_visits));
          unsigned reported_unknown = 0;
          for (const auto &stamp : result.read_set)
            if ((stamp.readsContent || stamp.readsLight) &&
                !stamp.incarnation &&
                stamp.missingEvidence != MissingChunkEvidence::KnownAir)
            {
              CubatariumLogInfo("RelightUnknownInput",
                                "job=" + std::to_string(job_id) + " coord=(" +
                                    std::to_string(stamp.coord.x) + "," +
                                    std::to_string(stamp.coord.y) + "," +
                                    std::to_string(stamp.coord.z) + ")");
              if (++reported_unknown == 8)
                break;
            }
        }
        result.job_id = job_id;
        result.submitEpoch = submit_epoch;
        result.work_token = snapshot.GetWorkToken();
        result.work_token.world_epoch = submit_epoch;
        result.dependency_stamp = snapshot.GetDependencyStamp();
        result.input_catalog = std::move(catalogKeep);
        RelightComputeResult dropped;
        if (Completed.PushDropOldest(std::move(result), &dropped))
        {
          NoteCompletedOverflow(std::move(dropped));
        }
      }))
    ReturnUnsubmittedJob(job_id, submit_epoch);
  const auto submit_complete = std::chrono::steady_clock::now();
  timings.queue_submit_ms =
      std::chrono::duration<double, std::milli>(submit_complete -
                                                submit_started)
          .count();
  timings.submit_setup_ms =
      std::chrono::duration<double, std::milli>(submit_started -
                                                dependency_complete)
          .count();
  return timings;
}

void UAsyncRelightBuilder::ReturnUnsubmittedJob(uint64_t job_id, uint64_t submit_epoch)
{
  std::lock_guard<std::mutex> owner_lock(InFlightMutex);
  const auto pending = RetryObligations.find(job_id);
  if (pending == RetryObligations.end() || pending->second.epoch != submit_epoch)
    return; // Cancellation already transferred this exact submission.
  std::lock_guard<std::mutex> debt_lock(DiscardedSourcesMutex);
  DiscardedJobs.push_back(std::move(pending->second.spec));
  RetryObligations.erase(pending);
  InFlight.erase(job_id);
}

std::vector<RelightComputeResult>
UAsyncRelightBuilder::DrainCompleted(int max_per_frame)
{
  // ColdSupply S0: honor Apply budget (Enter/Cruise). DrainAll made Enter×64
  // and Cruise ladder no-ops while MarkRelit hitch still scaled with ready N.
  if (max_per_frame <= 0)
  {
    return {};
  }
  std::vector<RelightComputeResult> drained =
      Completed.DrainUpTo(static_cast<std::size_t>(max_per_frame));
  const uint64_t current_epoch = Epoch.load(std::memory_order_acquire);
  std::vector<RelightComputeResult> accepted;
  accepted.reserve(drained.size());
  {
    std::lock_guard<std::mutex> lock(InFlightMutex);
    for (RelightComputeResult &result : drained)
    {
      if (result.submitEpoch != current_epoch)
      {
        DiscardedLate.fetch_add(1, std::memory_order_relaxed);
        // Cancel already transferred this obligation. A late completion must
        // neither duplicate it nor erase a successor reusing the same ID.
        continue;
      }
      const auto submitted = InFlight.find(result.job_id);
      if (submitted != InFlight.end() && result.inputs_complete &&
          !result.chunks.empty())
      {
        const double now_ms =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count();
        SampleDeliveryBudget.Observe(now_ms, now_ms - submitted->second);
      }
      InFlight.erase(result.job_id);
      RetryObligations.erase(result.job_id);
      accepted.push_back(std::move(result));
    }
  }
  return accepted;
}

bool UAsyncRelightBuilder::HasPendingWork() const
{
  if (!Completed.Empty())
  {
    return true;
  }
  std::lock_guard<std::mutex> lock(InFlightMutex);
  if (!InFlight.empty()) return true;
  // Admission refusal has no executable owner, but its undelivered exact
  // request must still prevent a false quiescent/readiness observation.
  std::lock_guard<std::mutex> discarded_lock(DiscardedSourcesMutex);
  if (!DiscardedJobs.empty()) return true;
  std::lock_guard<std::mutex> overflow_lock(OverflowMutex);
  return !OverflowJobs.empty();
}

double UAsyncRelightBuilder::GetLightSampleHorizonSeconds() const
{
  const double now_ms = std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();
  return SampleDeliveryBudget.HorizonSeconds(now_ms, GetOldestInFlightAgeMs());
}

int UAsyncRelightBuilder::GetInFlightCount() const
{
  std::lock_guard<std::mutex> lock(InFlightMutex);
  return static_cast<int>(InFlight.size());
}

bool UAsyncRelightBuilder::HasInFlightJob(uint64_t job_id) const
{
  std::lock_guard<std::mutex> lock(InFlightMutex);
  return InFlight.count(job_id) != 0;
}

double UAsyncRelightBuilder::GetOldestInFlightAgeMs() const
{
  std::lock_guard<std::mutex> lock(InFlightMutex);
  if (InFlight.empty())
  {
    return 0.0;
  }
  const int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
  int64_t oldest = now_ms;
  for (const auto &entry : InFlight)
  {
    oldest = std::min(oldest, entry.second);
  }
  return static_cast<double>(now_ms - oldest);
}

void UAsyncRelightBuilder::WaitIdle() { Pool.WaitIdle(); }

bool UAsyncRelightBuilder::WaitIdleFor(const std::chrono::milliseconds timeout)
{
  return Pool.WaitIdleFor(timeout);
}

void UAsyncRelightBuilder::CancelPending()
{
  SampleDeliveryBudget.Reset();
  Epoch.fetch_add(1, std::memory_order_acq_rel);
  Pool.CancelPendingJobs();
  {
    std::lock_guard<std::mutex> lock(InFlightMutex);
    std::lock_guard<std::mutex> src_lock(DiscardedSourcesMutex);
    for (auto &entry : RetryObligations)
      DiscardedJobs.push_back(std::move(entry.second.spec));
    RetryObligations.clear();
    InFlight.clear();
  }
  const uint64_t current_epoch = Epoch.load(std::memory_order_acquire);
  for (RelightComputeResult &result : Completed.DrainAll())
  {
    if (result.submitEpoch != current_epoch)
    {
      DiscardedLate.fetch_add(1, std::memory_order_relaxed);
    }
  }
}

std::vector<glm::ivec3> UAsyncRelightBuilder::TakeDiscardedSourcePositions()
{
  std::vector<glm::ivec3> out;
  for (auto &spec : TakeDiscardedJobs())
    out.insert(out.end(), spec.block_positions.begin(), spec.block_positions.end());
  return out;
}

std::vector<RelightJobSpec> UAsyncRelightBuilder::TakeDiscardedJobs()
{
  std::lock_guard<std::mutex> lock(DiscardedSourcesMutex);
  std::vector<RelightJobSpec> out;
  out.swap(DiscardedJobs);
  return out;
}

void UAsyncRelightBuilder::NoteCompletedOverflow(RelightComputeResult &&dropped)
{
  std::lock_guard<std::mutex> lock(InFlightMutex);
  const auto found = RetryObligations.find(dropped.job_id);
  if (found == RetryObligations.end() ||
      found->second.epoch != dropped.submitEpoch)
    return; // Already transferred by cancel, or belongs to an older epoch.
  std::lock_guard<std::mutex> olock(OverflowMutex);
  OverflowJobs.push_back(std::move(found->second.spec));
  RetryObligations.erase(found);
  InFlight.erase(dropped.job_id);
}

std::vector<glm::ivec3> UAsyncRelightBuilder::TakeOverflowSourcePositions()
{
  std::vector<glm::ivec3> out;
  for (auto &spec : TakeOverflowJobs())
    out.insert(out.end(), spec.block_positions.begin(), spec.block_positions.end());
  return out;
}

std::vector<RelightJobSpec> UAsyncRelightBuilder::TakeOverflowJobs()
{
  std::lock_guard<std::mutex> lock(OverflowMutex);
  std::vector<RelightJobSpec> out;
  out.swap(OverflowJobs);
  return out;
}

} // namespace cutum
