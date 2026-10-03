#include "World/Lighting/AsyncRelightBuilder.h"

#include "Blocks/BlockRegistry.h"
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
  WorkToken work_token = snapshot.GetWorkToken();
  work_token.world_epoch = submit_epoch;
  work_token.domain = WorkDomain::Relight;
  work_token.generation = job_id;
  snapshot.SetSubmitContext(work_token, snapshot.GetDependencyStamp());
  {
    std::lock_guard<std::mutex> lock(InFlightMutex);
    InFlight[job_id] = job_id;
  }

  auto catalogKeep = registry.GetDefinitionsCatalogSnapshot();

  Pool.Enqueue([this, snapshot = std::move(snapshot), registryPtr = &registry,
                catalogKeep = std::move(catalogKeep), job_id,
                submit_epoch]() mutable
               {
                 // Catalog pin proves immutable input identity for install checks;
                 // Compute still needs registry physics maps (catalog-only flood = follow-on).
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
               });
}

void UAsyncRelightBuilder::EnqueueJob(const UBlockWorld &world,
                                      RelightJobSpec spec,
                                      const UBlockRegistry &registry)
{
  const bool audit_relight =
      std::getenv("CUBATARIUM_RELIGHT_AUDIT") != nullptr;
  const auto capture_started = std::chrono::steady_clock::now();
  const uint64_t submit_epoch = Epoch.load(std::memory_order_acquire);
  const uint64_t job_id =
      spec.job_id > 0 ? spec.job_id
                      : NextJobId.fetch_add(1, std::memory_order_relaxed);
  spec.job_id = job_id;

  UChunkRelightSnapshot snapshot;
  {
    std::lock_guard<std::mutex> capture_lock(gRelightCaptureMutex);
    snapshot = UChunkRelightSnapshot::Capture(world, spec);
  }
  LastCaptureFullN = snapshot.GetCapturedFullChunks();
  LastCaptureNeighborLightN = snapshot.GetCapturedNeighborLightChunks();
  WorkToken work_token;
  work_token.world_epoch = submit_epoch;
  work_token.domain = WorkDomain::Relight;
  work_token.generation = job_id;
  DependencyStamp deps;
  if (!spec.block_positions.empty())
  {
    const glm::ivec3 chunk =
        UChunkManager::WorldToChunk(spec.block_positions.front());
    work_token.coord = chunk;
    work_token.chunk_incarnation = ChunkIncarnationAt(world, chunk);
    deps = BuildRelightDependencyStamp(world, chunk, registry);
  }
  snapshot.SetSubmitContext(work_token, deps);
  const double capture_ms = std::chrono::duration<double, std::milli>(
                                std::chrono::steady_clock::now() - capture_started)
                                .count();
  const int captured_full_n = snapshot.GetCapturedFullChunks();
  const int captured_neighbor_light_n =
      snapshot.GetCapturedNeighborLightChunks();
  if (audit_relight)
  {
    const glm::ivec3 source = spec.block_positions.empty()
                                  ? glm::ivec3(-1)
                                  : spec.block_positions.front();
    CubatariumLogInfo(
        "RelightAudit",
        "snapshot job=" + std::to_string(job_id) + " source=(" +
            std::to_string(source.x) + "," + std::to_string(source.y) + "," +
            std::to_string(source.z) + ") band=" +
            std::to_string(spec.min_world_y) + ":" +
            std::to_string(spec.max_world_y) + " capture_ms=" +
            std::to_string(capture_ms) + " full=" +
            std::to_string(captured_full_n) + " neighbor_light=" +
            std::to_string(captured_neighbor_light_n) + " finalize=" +
            std::to_string(spec.finalize_pending_gate) + " draw_gate=" +
            std::to_string(spec.visible_draw_gate_repair));
  }

  {
    std::lock_guard<std::mutex> lock(InFlightMutex);
    InFlight[job_id] = job_id;
  }

  auto catalogKeep = registry.GetDefinitionsCatalogSnapshot();

  // A21 P5: share work-slot envelope with mesh workers. If denied, still
  // enqueue (do not drop light demand) under a limited bypass counter —
  // separate relight pool cutover is follow-on.
  struct WorkSlotGuard
  {
    bool held{false};
    ~WorkSlotGuard()
    {
      if (held)
      {
        UPipelineAdmission::Get().ReleaseWorkSlot();
      }
    }
  };
  auto work_slot = std::make_shared<WorkSlotGuard>();
  work_slot->held = UPipelineAdmission::Get().TryAcquireWorkSlot();
  if (!work_slot->held)
  {
    static std::atomic<uint64_t> gRelightWorkSlotBypassN{0};
    gRelightWorkSlotBypassN.fetch_add(1, std::memory_order_relaxed);
  }

  Pool.Enqueue([this, snapshot = std::move(snapshot), registry = &registry,
                catalogKeep = std::move(catalogKeep), job_id, submit_epoch,
                work_slot = std::move(work_slot), audit_relight,
                captured_full_n, captured_neighbor_light_n]() mutable
               {
                 (void)work_slot;
                 const auto compute_started = std::chrono::steady_clock::now();
                 if (audit_relight)
                 {
                   CubatariumLogInfo("RelightAudit",
                       "worker_start job=" + std::to_string(job_id));
                 }
                 RelightComputeResult result = snapshot.Compute(*registry);
                 const double compute_ms = std::chrono::duration<double, std::milli>(
                                               std::chrono::steady_clock::now() - compute_started)
                                               .count();
                 if (audit_relight)
                 {
                   CubatariumLogInfo(
                       "RelightAudit",
                       "worker_done job=" + std::to_string(job_id) +
                           " compute_ms=" + std::to_string(compute_ms) +
                           " full=" + std::to_string(captured_full_n) +
                           " neighbor_light=" +
                           std::to_string(captured_neighbor_light_n) +
                           " chunks=" + std::to_string(result.chunks.size()) +
                           " read_set=" + std::to_string(result.read_set.size()) +
                           " frontier_unfinished=" +
                           std::to_string(result.frontier_unfinished));
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
               });
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
        InFlight.erase(result.job_id);
        {
          std::lock_guard<std::mutex> src_lock(DiscardedSourcesMutex);
          DiscardedSources.insert(DiscardedSources.end(),
                                  result.source_block_positions.begin(),
                                  result.source_block_positions.end());
        }
        continue;
      }
      InFlight.erase(result.job_id);
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
  return !InFlight.empty();
}

int UAsyncRelightBuilder::GetInFlightCount() const
{
  std::lock_guard<std::mutex> lock(InFlightMutex);
  return static_cast<int>(InFlight.size());
}

void UAsyncRelightBuilder::WaitIdle() { Pool.WaitIdle(); }

bool UAsyncRelightBuilder::WaitIdleFor(const std::chrono::milliseconds timeout)
{
  return Pool.WaitIdleFor(timeout);
}

void UAsyncRelightBuilder::CancelPending()
{
  Epoch.fetch_add(1, std::memory_order_acq_rel);
  Pool.CancelPendingJobs();
  {
    std::lock_guard<std::mutex> lock(InFlightMutex);
    InFlight.clear();
  }
  const uint64_t current_epoch = Epoch.load(std::memory_order_acquire);
  for (RelightComputeResult &result : Completed.DrainAll())
  {
    if (result.submitEpoch != current_epoch)
    {
      DiscardedLate.fetch_add(1, std::memory_order_relaxed);
      std::lock_guard<std::mutex> src_lock(DiscardedSourcesMutex);
      DiscardedSources.insert(DiscardedSources.end(),
                              result.source_block_positions.begin(),
                              result.source_block_positions.end());
    }
  }
}

std::vector<glm::ivec3> UAsyncRelightBuilder::TakeDiscardedSourcePositions()
{
  std::lock_guard<std::mutex> src_lock(DiscardedSourcesMutex);
  std::vector<glm::ivec3> out;
  out.swap(DiscardedSources);
  return out;
}

void UAsyncRelightBuilder::NoteCompletedOverflow(RelightComputeResult &&dropped)
{
  {
    std::lock_guard<std::mutex> lock(InFlightMutex);
    InFlight.erase(dropped.job_id);
  }
  std::vector<glm::ivec3> sources = std::move(dropped.source_block_positions);
  if (sources.empty())
  {
    for (const RelightChunkLightData &chunk : dropped.chunks)
    {
      sources.push_back(glm::ivec3(chunk.coord.x * CHUNK_SIZE,
                                   chunk.coord.y * CHUNK_SIZE,
                                   chunk.coord.z * CHUNK_SIZE));
    }
  }
  std::lock_guard<std::mutex> olock(OverflowMutex);
  OverflowSources.insert(OverflowSources.end(), sources.begin(), sources.end());
}

std::vector<glm::ivec3> UAsyncRelightBuilder::TakeOverflowSourcePositions()
{
  std::lock_guard<std::mutex> olock(OverflowMutex);
  std::vector<glm::ivec3> out;
  out.swap(OverflowSources);
  return out;
}

} // namespace cutum
