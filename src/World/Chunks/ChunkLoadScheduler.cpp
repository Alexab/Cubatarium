#include "World/Chunks/ChunkLoadScheduler.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/BlockWorld.h"
#include "App/Platform/Log.h"
#include "Core/Jobs/JobThreadBudget.h"
#include "Core/Jobs/PipelineAdmission.h"
#include "World/Streaming/WorldColumnRequestId.h"
#include "WorldGen/Core/WorldGenContentPin.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <limits>
#include <string>
#include <thread>

namespace cutum
{

namespace
{

size_t StringCapacityCharge(const std::string &value)
{
  const auto data=reinterpret_cast<uintptr_t>(value.data());
  const auto object=reinterpret_cast<uintptr_t>(&value);
  return data>=object && data-object<sizeof(value) ? 0 : value.capacity()+1;
}
size_t SettingsStringCharge(const ProceduralSettings &settings)
{
  size_t charge=0;
  for(const auto *value:{&settings.SeedText,&settings.WorldGenPackId,&settings.WorldGenPresetId}) {
    const size_t next=StringCapacityCharge(*value);
    if(next>std::numeric_limits<size_t>::max()-charge) return std::numeric_limits<size_t>::max();
    charge+=next;
  }
  return charge;
}

bool IsWorldColumnSourceTraceEnabled()
{
  return WorldColumnSourceTraceEnabled();
}

void LogProceduralRequest(const char *outcome, glm::ivec3 coord,
                          uint64_t request_id, uint64_t demand_id,
                          const std::string &details = {})
{
  if (!IsWorldColumnSourceTraceEnabled())
  {
    return;
  }
  std::string message =
      "source=procedural outcome=" + std::string(outcome) + " coord=(" +
      std::to_string(coord.x) + ",0," + std::to_string(coord.z) +
      ") request_id=" + std::to_string(request_id) +
      " demand_id=" + std::to_string(demand_id);
  if (!details.empty())
  {
    message += " " + details;
  }
  CubatariumLogInfo("WorldColumnSource", message);
}

} // namespace

UChunkLoadScheduler::UChunkLoadScheduler(IUChunkPopulator &populator,
                                         UChunkGenerationRegistry &tokens)
    : Populator(populator), Tokens(tokens),
      Pool(ComputeWorkerThreadCount(JobPoolKind::ChunkGeneration),
           "ChunkGeneration")
{
  // The retained-result lease covers queued/running/Ready/Tick owners, so
  // this lossless queue never needs more than these preallocated slots.
  Completed.ReserveStorage(kMaxRetainedGenerationResults);
}

void UChunkLoadScheduler::SetMarkDirtyFn(MarkChunkDirtyFn fn)
{
  MarkDirty = std::move(fn);
}

void UChunkLoadScheduler::SetColumnMeshDirtyFn(ColumnMeshDirtyFn fn)
{
  ColumnMeshDirty = std::move(fn);
}

bool UChunkLoadScheduler::RequestLoad(glm::ivec3 coord, int priority,
                                      const ProceduralSettings &settings,
                                      glm::ivec2 column_origin,
                                      bool has_column_origin,
                                      uint64_t demand_id)
{
  if (coord.y != 0)
  {
    return false;
  }
  const auto stateIt = States.find(coord);
  if (stateIt != States.end())
  {
    if (stateIt->second.state == ChunkLoadState::Requested)
    {
      const auto queuedIt = QueuedRequests.find(coord);
      if (queuedIt != QueuedRequests.end())
      {
        if (priority == queuedIt->second.priority)
        {
          return true;
        }
        // Re-rank in both directions as the focus moves. Preserve the original
        // request time/settings; resetting them would hide queue wait and make
        // generated columns depend on which camera update happened last.
        PendingRequest updated = queuedIt->second;
        updated.priority = priority;
        updated.queueRevision = NextRequestQueueRevision++;
        Queue.erase(queuedIt->second);
        queuedIt->second = updated;
        RequestPriorities[coord] = priority;
        ++RequestPriorityRefreshCounts[coord];
        Queue.insert(std::move(updated));
        LogProceduralRequest(
            "priority_refreshed", coord, queuedIt->second.requestId,
            demand_id, "state=requested priority=" + std::to_string(priority));
        return true;
      }
    }
    else if (stateIt->second.state == ChunkLoadState::Generating ||
             stateIt->second.state == ChunkLoadState::Ready)
    {
      // Job already running / ready: refresh live priority in either direction
      // so stale near-focus work cannot retain its old commit preference.
      const auto prioIt = RequestPriorities.find(coord);
      if (prioIt != RequestPriorities.end() && priority == prioIt->second)
      {
        return true;
      }
      if (InitialRequestPriorities.find(coord) ==
          InitialRequestPriorities.end())
      {
        InitialRequestPriorities[coord] =
            prioIt != RequestPriorities.end() ? prioIt->second : priority;
      }
      ++RequestPriorityRefreshCounts[coord];
      RequestPriorities[coord] = priority;
      const auto idIt = ActiveRequestIds.find(coord);
      LogProceduralRequest(
          "priority_refreshed", coord,
          idIt != ActiveRequestIds.end() ? idIt->second : 0, demand_id,
          "state=" + std::to_string(static_cast<int>(stateIt->second.state)) +
              " priority=" + std::to_string(priority));
      return true;
    }
    else if (stateIt->second.state != ChunkLoadState::Absent)
    {
      return true;
    }
  }
  if (ActiveTokens.size() >= kMaxActiveGenerationRequests || States.size() >= MetadataCapacity)
  {
    ++CapacityRefused;
    return false;
  }
  auto owned=std::make_shared<OwnedSettings>(settings);
  const size_t fixed=sizeof(OwnedSettings)+sizeof(ProceduralSettings);
  const size_t strings=SettingsStringCharge(owned->value);
  if(strings>(std::numeric_limits<size_t>::max()-fixed)/2) return false;
  const size_t charge=fixed+2*strings; // immutable snapshot and one worker value copy
  SettingsBudget->largest_single=std::max(SettingsBudget->largest_single,charge);
  size_t used=SettingsBudget->used.load(std::memory_order_relaxed);
  do {
    const size_t limit=SettingsPayloadLimit();
    if(used>limit || charge>limit-used) { ++CapacityRefused; return false; }
  } while(!SettingsBudget->used.compare_exchange_weak(used,used+charge,std::memory_order_relaxed));
  owned->budget=SettingsBudget; owned->charge=charge;
  size_t peak=SettingsBudget->peak.load(std::memory_order_relaxed);
  while(peak<used+charge && !SettingsBudget->peak.compare_exchange_weak(
      peak,used+charge,std::memory_order_relaxed)) {}
  PendingRequest pending;
  pending.coord = coord;
  pending.requestId = IsWorldColumnSourceTraceEnabled()
                          ? NextWorldColumnRequestId()
                          : 0;
  pending.demandId = demand_id;
  pending.priority = priority;
  pending.token = Tokens.EnsureCurrent(coord);
  pending.requestedAt = std::chrono::steady_clock::now();
  pending.queueRevision = NextRequestQueueRevision++;
  pending.settings = std::move(owned);
  pending.columnOrigin = column_origin;
  pending.hasColumnOrigin = has_column_origin;
  const uint64_t request_id = pending.requestId;
  States[coord] = {ChunkLoadState::Requested,pending.token.sequence};
  ActiveTokens[coord] = pending.token;
  if (pending.requestId != 0)
  {
    ActiveRequestIds[coord] = pending.requestId;
  }
  RequestPriorities[coord] = priority;
  InitialRequestPriorities[coord] = priority;
  RequestPriorityRefreshCounts[coord] = 0;
  QueuedRequests[coord] = pending;
  Queue.insert(std::move(pending));
  LogProceduralRequest("requested", coord, request_id, demand_id,
                       "priority=" + std::to_string(priority));
  return true;
}

void UChunkLoadScheduler::Cancel(glm::ivec3 coord, const char *reason)
{
  const auto id_it = ActiveRequestIds.find(coord);
  if (id_it != ActiveRequestIds.end())
  {
    LogProceduralRequest(
        "cancelled", coord, id_it->second, 0,
        "reason=" + std::string(reason ? reason : "cancelled"));
  }
  // Retire this submission before forgetting it: a late computed result must
  // not match a replacement request that reads the registry's current token.
  // Invalidate (or another owner) may already have advanced the registry.
  const auto token_it = ActiveTokens.find(coord);
  if (token_it != ActiveTokens.end())
    Tokens.Retire(coord, token_it->second.sequence);
  const auto state = States.find(coord);
  if (state != States.end()) Tokens.Retire(coord,state->second.sequence);
  States.erase(coord);
  ActiveTokens.erase(coord);
  ActiveRequestIds.erase(coord);
  RequestPriorities.erase(coord);
  InitialRequestPriorities.erase(coord);
  RequestPriorityRefreshCounts.erase(coord);
  EraseQueuedRequest(coord);
}

int UChunkLoadScheduler::CancelPendingOutsideRadius(glm::ivec3 center,
                                                     int radius_chunks)
{
  center.y = 0;
  const int radius = std::max(0, radius_chunks);
  std::vector<glm::ivec3> stale;
  for (const auto &entry : ActiveTokens)
  {
    const glm::ivec3 coord = entry.first;
    const int distance = std::max(std::abs(coord.x - center.x),
                                  std::abs(coord.z - center.z));
    if (distance > radius)
    {
      stale.push_back(coord);
    }
  }
  if (stale.empty())
  {
    return 0;
  }

  const bool trace = IsWorldColumnSourceTraceEnabled();
  for (const glm::ivec3 coord : stale)
  {
    const int distance = std::max(std::abs(coord.x - center.x),
                                  std::abs(coord.z - center.z));
    const auto state_it = States.find(coord);
    const int state = state_it == States.end()
                          ? static_cast<int>(ChunkLoadState::Absent)
                          : static_cast<int>(state_it->second.state);
    const auto priority_it = RequestPriorities.find(coord);
    const int priority = priority_it == RequestPriorities.end()
                             ? 0
                             : priority_it->second;
    const auto initial_it = InitialRequestPriorities.find(coord);
    const int initial_priority = initial_it == InitialRequestPriorities.end()
                                     ? priority
                                     : initial_it->second;
    if (trace)
    {
      const auto id_it = ActiveRequestIds.find(coord);
      LogProceduralRequest(
          "cancelled_out_of_range", coord,
          id_it != ActiveRequestIds.end() ? id_it->second : 0, 0,
          "distance_chunks=" + std::to_string(distance) +
              " radius_chunks=" + std::to_string(radius) +
              " state=" + std::to_string(state) +
              " priority_initial=" + std::to_string(initial_priority) +
              " priority_current=" + std::to_string(priority));
    }
    // Invalidate the token before removing the owner so active worldgen workers
    // can stop at their next cancellation point instead of filling the ready
    // queue with terrain the camera has already left behind.
    Tokens.Retire(coord, ActiveTokens.at(coord).sequence);
    States.erase(coord);
    ActiveTokens.erase(coord);
    ActiveRequestIds.erase(coord);
    RequestPriorities.erase(coord);
    InitialRequestPriorities.erase(coord);
    RequestPriorityRefreshCounts.erase(coord);
    EraseQueuedRequest(coord);
  }
  return static_cast<int>(stale.size());
}

void UChunkLoadScheduler::EraseQueuedRequest(glm::ivec3 coord)
{
  const auto request = QueuedRequests.find(coord);
  if (request == QueuedRequests.end()) return;
  Queue.erase(request->second);
  QueuedRequests.erase(request);
}

void UChunkLoadScheduler::CancelAllPending(
    const std::chrono::milliseconds worker_wait, const char *reason)
{
  if (IsWorldColumnSourceTraceEnabled())
  {
    std::size_t requested = 0;
    std::size_t generating = 0;
    std::size_t ready = 0;
    std::size_t committed = 0;
    for (const auto &entry : States)
    {
      switch (entry.second.state)
      {
      case ChunkLoadState::Requested:
        ++requested;
        break;
      case ChunkLoadState::Generating:
        ++generating;
        break;
      case ChunkLoadState::Ready:
        ++ready;
        break;
      case ChunkLoadState::Committed:
        ++committed;
        break;
      case ChunkLoadState::Absent:
        break;
      }
    }
    const JobThreadPoolSnapshot workers = Pool.GetSnapshot();
    CubatariumLogInfo(
        "WorldColumnSource",
        "source=procedural outcome=shutdown_snapshot reason=" +
            std::string(reason ? reason : "cancelled_all") +
            " requested_n=" + std::to_string(requested) +
            " generating_n=" + std::to_string(generating) +
            " ready_n=" + std::to_string(ready) +
            " committed_n=" + std::to_string(committed) +
            " scheduler_queue_n=" + std::to_string(QueuedRequests.size()) +
            " worker_pending_n=" + std::to_string(workers.pending) +
            " worker_active_n=" + std::to_string(workers.active) +
            " completed_ready_n=" + std::to_string(Completed.Size()));
  }
  for (const auto &entry : ActiveTokens)
  {
    const auto id_it = ActiveRequestIds.find(entry.first);
    LogProceduralRequest(
        "cancelled", entry.first,
        id_it != ActiveRequestIds.end() ? id_it->second : 0, 0,
        "reason=" + std::string(reason ? reason : "cancelled_all"));
  }
  Queue.clear();
  for (const auto &entry : ActiveTokens)
    Tokens.Retire(entry.first,entry.second.sequence);
  for (const auto &entry : States)
  {
    if (entry.second.state == ChunkLoadState::Committed)
      Tokens.Retire(entry.first,entry.second.sequence);
  }
  States.clear();
  MetadataRetirementCursor.reset();
  ActiveTokens.clear();
  ActiveRequestIds.clear();
  RequestPriorities.clear();
  InitialRequestPriorities.clear();
  RequestPriorityRefreshCounts.clear();
  QueuedRequests.clear();
  // Release historical bucket capacity, retaining the same resource owner.
  decltype(QueuedRequests){&MetadataMemory}.swap(QueuedRequests);
  decltype(ActiveTokens){&MetadataMemory}.swap(ActiveTokens);
  decltype(ActiveRequestIds){&MetadataMemory}.swap(ActiveRequestIds);
  decltype(RequestPriorities){&MetadataMemory}.swap(RequestPriorities);
  decltype(InitialRequestPriorities){&MetadataMemory}.swap(InitialRequestPriorities);
  decltype(RequestPriorityRefreshCounts){&MetadataMemory}.swap(RequestPriorityRefreshCounts);
  Pool.CancelPendingJobs();
  (void)Completed.DrainAll();
  if (worker_wait.count() > 0)
  {
    (void)Pool.WaitIdleFor(worker_wait);
  }
  (void)Completed.DrainAll();
}

bool UChunkLoadScheduler::WaitForWorkersIdle(
    const std::chrono::milliseconds timeout)
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline)
  {
    (void)Completed.DrainAll();
    if (Pool.GetActiveJobCount() == 0 && Queue.empty())
    {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return Pool.GetActiveJobCount() == 0 && Queue.empty();
}

void UChunkLoadScheduler::ShutdownForProcessExit(
    const std::chrono::milliseconds timeout)
{
  CancelAllPending(std::chrono::milliseconds(0), "process_shutdown");
  Pool.ShutdownForProcessExit(timeout);
  (void)Completed.DrainAll();
}

void UChunkLoadScheduler::Invalidate(glm::ivec3 coord)
{
  const auto current = Tokens.Current(coord);
  Tokens.Retire(coord,current.sequence);
  Cancel(coord, "invalidated");
}

bool UChunkLoadScheduler::ScheduleWorker(
    const PendingRequest &request, const int generation_start_cap_per_frame)
{
  if((BufferSerialMode.load(std::memory_order_relaxed) && GetRetainedGenerationResultCount()!=0) ||
      BufferBudget->UsedBytes()>=BufferBudget->LimitBytes()) return false;
  size_t retained = RetainedResults->load(std::memory_order_relaxed);
  do
  {
    if (retained >= kMaxRetainedGenerationResults) return false;
  } while (!RetainedResults->compare_exchange_weak(retained,retained+1,
      std::memory_order_relaxed));
  auto result_slot = std::allocate_shared<ResultSlotGuard>(USharedCountingAllocator<ResultSlotGuard>{ControlMemory});
  result_slot->count = RetainedResults;
  if (!UPipelineAdmission::Get().TryAcquireWorkSlot()) return false;
  struct WorkSlotGuard
  {
    ~WorkSlotGuard() { UPipelineAdmission::Get().ReleaseWorkSlot(); }
  };
  auto work_slot = std::allocate_shared<WorkSlotGuard>(USharedCountingAllocator<WorkSlotGuard>{ControlMemory});
  ChunkPopulateRequest populateRequest;
  populateRequest.chunkCoord = request.coord;
  populateRequest.token = request.token;
  populateRequest.settings = request.settings->value;
  populateRequest.columnOrigin = request.columnOrigin;
  populateRequest.hasColumnOrigin = request.hasColumnOrigin;
  const glm::ivec3 coord = request.coord;
  const uint64_t start_sequence = request.token.sequence;
  populateRequest.shouldCancel = [this, coord, start_sequence]()
  { return Tokens.Current(coord).sequence != start_sequence; };
  populateRequest.content = CaptureWorldGenContentSnapshot();
  const auto requested_at = request.requestedAt;
  const auto scheduled_at = std::chrono::steady_clock::now();
  const int priority = request.priority;
  const uint64_t request_id = request.requestId;
  const uint64_t demand_id = request.demandId;
  const int max_height = request.settings->value.MaxHeight;
  const std::size_t request_queue_live_at_schedule =
      QueuedRequests.empty() ? 0 : QueuedRequests.size() - 1;
  const std::size_t request_queue_heap_at_schedule = Queue.size();
  const std::size_t worker_pending_at_submit = Pool.GetPendingJobCount();
  const std::size_t worker_active_at_submit = Pool.GetActiveJobCount();
  const std::size_t worker_count = Pool.GetWorkerCount();
  auto worker =
      [this, populateRequest = std::move(populateRequest), settings_owner = request.settings,
       retry_request=request, buffer_budget=BufferBudget, control_memory=ControlMemory,
       requested_at, scheduled_at, priority,
       request_id, demand_id,
       max_height, request_queue_live_at_schedule,
       request_queue_heap_at_schedule, worker_pending_at_submit,
       worker_active_at_submit, worker_count,
       generation_start_cap_per_frame, work_slot = std::move(work_slot), result_slot]() mutable
      {
        PendingResult pending;
        pending.resultSlot = result_slot;
        pending.requestId = request_id;
        pending.demandId = demand_id;
        pending.priority = priority;
        pending.maxHeight = max_height;
        pending.requestedAt = requested_at;
        pending.scheduledAt = scheduled_at;
        pending.generationStartedAt = std::chrono::steady_clock::now();
        const auto generation_started = pending.generationStartedAt;
        pending.requestQueueLiveAtSchedule =
            request_queue_live_at_schedule;
        pending.requestQueueHeapAtSchedule =
            request_queue_heap_at_schedule;
        pending.workerPendingAtSubmit = worker_pending_at_submit;
        pending.workerActiveAtSubmit = worker_active_at_submit;
        pending.workerCount = worker_count;
        pending.generationStartCapPerFrame =
            generation_start_cap_per_frame;
        try {
          UBufferBudgetScope budget_scope({buffer_budget,std::allocate_shared<BufferBudgetJob>(
              USharedCountingAllocator<BufferBudgetJob>{control_memory})});
          pending.result = Populator.Populate(populateRequest);
        } catch(const UBufferBudgetExceeded &) {
          pending.result.coord=populateRequest.chunkCoord;
          pending.result.token=populateRequest.token;
          pending.bufferRetry=std::move(retry_request);
          BufferSerialMode.store(true,std::memory_order_relaxed);
        }
        pending.generationFinishedAt = std::chrono::steady_clock::now();
        pending.generationMs = std::chrono::duration<double, std::milli>(
                                   pending.generationFinishedAt -
                                   generation_started)
                                   .count();
        LogProceduralRequest(
            "worker_finished", pending.result.coord, pending.requestId,
            pending.demandId,
            "discarded=" + std::to_string(pending.result.discarded ? 1 : 0) +
                " generation_ms=" + std::to_string(pending.generationMs));
        Completed.Push(std::move(pending));
      };
  // Keep the full callable and its shared control block in tracked storage.
  // std::function receives only a small shared-owner dispatch wrapper.
  auto task=std::allocate_shared<decltype(worker)>(
      USharedCountingAllocator<decltype(worker)>{ControlMemory},std::move(worker));
  if (!Pool.TryEnqueue([task=std::move(task)] {(*task)();})) return false;
  States[request.coord].state = ChunkLoadState::Generating;
  LogProceduralRequest(
      "worker_submitted", request.coord, request.requestId,
      request.demandId, "priority=" + std::to_string(request.priority));
  return true;
}

void UChunkLoadScheduler::Tick(UBlockWorld &world, int maxCommitsPerFrame,
                               int maxGenerationStartsPerFrame,
                               double maxApplyMsPerFrame,
                               bool allowBoundedReadyDrain)
{
  LastTickApplyMs = 0.0;
  LastCommitsThisFrame = 0;
  StartQueuedGenerationWorkers(maxGenerationStartsPerFrame);

  std::vector<PendingResult> ready = Completed.DrainAll();
  TickBatchStorage.count=ready.size();
  TickBatchStorage.capacity_bytes=ready.capacity()*sizeof(PendingResult);
  TickBatchStorage.peak_capacity_bytes=std::max(TickBatchStorage.peak_capacity_bytes,
      TickBatchStorage.capacity_bytes);
  // During intentional movement, use the actual drained batch rather than a
  // racy queue snapshot taken by WorldStreaming before Tick. Bound both the
  // result count and synchronous ApplyTo + MarkDirty time; one commit may
  // exceed the target.
  if (allowBoundedReadyDrain && ready.size() > 1)
  {
    constexpr int kReadyDrainMaxCommitsPerFrame = 3;
    constexpr double kReadyDrainApplyBudgetMs = 12.0;
    maxCommitsPerFrame = std::max(
        maxCommitsPerFrame,
        std::min(kReadyDrainMaxCommitsPerFrame,
                 static_cast<int>(ready.size())));
    maxApplyMsPerFrame = std::max(maxApplyMsPerFrame,
                                  kReadyDrainApplyBudgetMs);
  }
  std::sort(ready.begin(), ready.end(),
            [this](const PendingResult &a, const PendingResult &b)
            {
              auto live_priority = [this](const PendingResult &pending) -> int
              {
                const auto it = RequestPriorities.find(pending.result.coord);
                if (it != RequestPriorities.end())
                {
                  return it->second;
                }
                return pending.priority;
              };
              return live_priority(a) < live_priority(b);
            });
  int committed = 0;
  for (PendingResult &pending : ready)
  {
    const auto tokenIt = ActiveTokens.find(pending.result.coord);
    if (tokenIt == ActiveTokens.end() ||
        !pending.result.token.IsValidFor(pending.result.coord,
                                         tokenIt->second.sequence))
    {
      // A stale worker must not erase or overwrite state for a newer request
      // at the same coordinate.
      LogProceduralRequest(
          "stale_worker_result", pending.result.coord, pending.requestId,
          pending.demandId, "stage=apply");
      continue;
    }
    // ActiveTokens records this scheduler's submission, not the current
    // cross-owner authority. Disk loading or another owner may advance the
    // registry after generation finished and before this ready buffer applies.
    if (!pending.result.token.IsValidFor(
            pending.result.coord, Tokens.Current(pending.result.coord).sequence))
    {
      LogProceduralRequest("stale_worker_result", pending.result.coord,
                           pending.requestId, pending.demandId,
                           "stage=apply_registry");
      // The preceding identity check proved this is still our exact old
      // submission. Retire its metadata without bumping the newer registry
      // token or interfering with another owner's terrain.
      States.erase(pending.result.coord);
      ActiveTokens.erase(tokenIt);
      ActiveRequestIds.erase(pending.result.coord);
      RequestPriorities.erase(pending.result.coord);
      InitialRequestPriorities.erase(pending.result.coord);
      RequestPriorityRefreshCounts.erase(pending.result.coord);
      const auto queued = QueuedRequests.find(pending.result.coord);
      if (queued != QueuedRequests.end() &&
          queued->second.token.sequence == pending.result.token.sequence)
        EraseQueuedRequest(pending.result.coord);
      continue;
    }
    if(pending.bufferRetry) {
      auto retry=std::move(*pending.bufferRetry);
      const auto priority=RequestPriorities.find(retry.coord);
      if(priority!=RequestPriorities.end()) retry.priority=priority->second;
      retry.queueRevision=NextRequestQueueRevision++;
      QueuedRequests[retry.coord]=retry;
      Queue.insert(std::move(retry));
      States[pending.result.coord].state=ChunkLoadState::Requested;
      continue;
    }
    if (pending.result.discarded)
    {
      LogProceduralRequest(
          "discarded", pending.result.coord, pending.requestId,
          pending.demandId, "stage=apply");
      Tokens.Retire(pending.result.coord,pending.result.token.sequence);
      States.erase(pending.result.coord);
      ActiveTokens.erase(tokenIt);
      ActiveRequestIds.erase(pending.result.coord);
      RequestPriorities.erase(pending.result.coord);
      InitialRequestPriorities.erase(pending.result.coord);
      RequestPriorityRefreshCounts.erase(pending.result.coord);
      continue;
    }
    States[pending.result.coord].state = ChunkLoadState::Ready;
    const bool apply_budget_exhausted =
        committed > 0 && maxApplyMsPerFrame > 0.0 &&
        LastTickApplyMs >= maxApplyMsPerFrame;
    if (committed >= maxCommitsPerFrame || apply_budget_exhausted)
    {
      Completed.Push(std::move(pending));
      continue;
    }
    const double ready_wait_ms = std::chrono::duration<double, std::milli>(
                                     std::chrono::steady_clock::now() -
                                     pending.generationFinishedAt)
                                     .count();
    const auto livePriorityIt = RequestPriorities.find(pending.result.coord);
    const int priorityAtCommit =
        livePriorityIt != RequestPriorities.end() ? livePriorityIt->second
                                                   : pending.priority;
    const auto initialPriorityIt =
        InitialRequestPriorities.find(pending.result.coord);
    const int initialPriority =
        initialPriorityIt != InitialRequestPriorities.end()
            ? initialPriorityIt->second
            : pending.priority;
    const auto priorityRefreshIt =
        RequestPriorityRefreshCounts.find(pending.result.coord);
    const uint64_t priorityRefreshCount =
        priorityRefreshIt != RequestPriorityRefreshCounts.end()
            ? priorityRefreshIt->second
            : 0;
    const auto apply_t0 = std::chrono::high_resolution_clock::now();
    pending.result.buffer.ApplyTo(world);
    States[pending.result.coord].state = ChunkLoadState::Committed;
    ActiveTokens.erase(pending.result.coord);
    ActiveRequestIds.erase(pending.result.coord);
    RequestPriorities.erase(pending.result.coord);
    InitialRequestPriorities.erase(pending.result.coord);
    RequestPriorityRefreshCounts.erase(pending.result.coord);
    int min_y = 0;
    int max_y = pending.maxHeight;
    if (pending.result.buffer.HasYBounds())
    {
      min_y = pending.result.buffer.GetMinY();
      max_y = pending.result.buffer.GetMaxY();
    }
    if (MarkDirty)
    {
      MarkDirty(pending.result.coord, min_y, max_y,
                pending.result.fluidSealed);
    }
    // Include MarkDirty in apply wall — previously invisible in stream_ms gap.
    const double apply_ms = std::chrono::duration<double, std::milli>(
                                std::chrono::high_resolution_clock::now() -
                                apply_t0)
                                .count();
    LastTickApplyMs += apply_ms;
    if (IsWorldColumnSourceTraceEnabled())
    {
      const double queue_ms = std::chrono::duration<double, std::milli>(
                                  pending.generationStartedAt -
                                  pending.requestedAt)
                                  .count();
      const double scheduler_queue_ms = std::chrono::duration<double, std::milli>(
                                            pending.scheduledAt -
                                            pending.requestedAt)
                                            .count();
      const double worker_pool_queue_ms = std::chrono::duration<double, std::milli>(
                                              pending.generationStartedAt -
                                              pending.scheduledAt)
                                              .count();
      const double total_ms = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() -
                                  pending.requestedAt)
                                  .count();
      const std::string message =
          "source=procedural outcome=committed coord=(" +
          std::to_string(pending.result.coord.x) + ",0," +
          std::to_string(pending.result.coord.z) + ") request_id=" +
          std::to_string(pending.requestId) + " demand_id=" +
          std::to_string(pending.demandId) + " priority=" +
          std::to_string(pending.priority) + " token=" +
          std::to_string(pending.result.token.sequence) + " queue_ms=" +
          std::to_string(queue_ms) + " scheduler_queue_ms=" +
          std::to_string(scheduler_queue_ms) + " worker_pool_queue_ms=" +
          std::to_string(worker_pool_queue_ms) +
          " request_queue_live_at_schedule=" +
          std::to_string(pending.requestQueueLiveAtSchedule) +
          " request_queue_heap_at_schedule=" +
          std::to_string(pending.requestQueueHeapAtSchedule) +
          " worker_pending_at_submit=" +
          std::to_string(pending.workerPendingAtSubmit) +
          " worker_active_at_submit=" +
          std::to_string(pending.workerActiveAtSubmit) +
          " worker_count=" + std::to_string(pending.workerCount) +
          " generation_start_cap_per_frame=" +
          std::to_string(pending.generationStartCapPerFrame) +
          " generation_ms=" +
          std::to_string(pending.generationMs) + " ready_wait_ms=" +
          std::to_string(ready_wait_ms) + " priority_initial=" +
          std::to_string(initialPriority) + " priority_at_schedule=" +
          std::to_string(pending.priority) + " priority_at_commit=" +
          std::to_string(priorityAtCommit) + " priority_refresh_n=" +
          std::to_string(priorityRefreshCount) + " ready_batch_n=" +
          std::to_string(ready.size()) + " max_commits_per_frame=" +
          std::to_string(maxCommitsPerFrame) + " max_apply_budget_ms=" +
          std::to_string(maxApplyMsPerFrame) + " apply_ms=" +
          std::to_string(apply_ms) + " total_ms=" + std::to_string(total_ms);
      CubatariumLogInfo("WorldColumnSource", message);
    }
    ++committed;
  }
  LastCommitsThisFrame = committed;
}

int UChunkLoadScheduler::StartQueuedGenerationWorkers(int maxStarts)
{
  if(Queue.empty() && GetRetainedGenerationResultCount()==0 && BufferBudget->UsedBytes()==0)
    BufferSerialMode.store(false,std::memory_order_relaxed);
  int generationStarts = 0;
  while (!Queue.empty() && generationStarts < maxStarts)
  {
    const PendingRequest next = *Queue.begin();
    Queue.erase(Queue.begin());
    const auto stateIt = States.find(next.coord);
    const auto queuedIt = QueuedRequests.find(next.coord);
    if (stateIt == States.end() ||
        stateIt->second.state != ChunkLoadState::Requested ||
        queuedIt == QueuedRequests.end() ||
        queuedIt->second.queueRevision != next.queueRevision)
    {
      continue;
    }
    const auto priorityIt = RequestPriorities.find(next.coord);
    if (priorityIt != RequestPriorities.end() &&
        next.priority != priorityIt->second)
    {
      continue;
    }
    if (!ScheduleWorker(next, maxStarts))
    {
      // Keep the exact immutable request and its rank. A future normal Tick
      // retries admission; do not spin on the same refused head this frame.
      Queue.insert(next);
      break;
    }
    QueuedRequests.erase(queuedIt);
    ++generationStarts;
  }
  return generationStarts;
}

int UChunkLoadScheduler::GetAvailableGenerationWorkerSlots() const
{
  const JobThreadPoolSnapshot snapshot = Pool.GetSnapshot();
  const std::size_t occupied = snapshot.pending + snapshot.active;
  if((BufferSerialMode.load(std::memory_order_relaxed) && GetRetainedGenerationResultCount()!=0) ||
      BufferBudget->UsedBytes()>=BufferBudget->LimitBytes()) return 0;
  const size_t retained = GetRetainedGenerationResultCount();
  return static_cast<int>(std::min(
      snapshot.workers > occupied ? snapshot.workers - occupied : size_t{0},
      retained < kMaxRetainedGenerationResults ? kMaxRetainedGenerationResults-retained : size_t{0}));
}

UChunkLoadScheduler::MetadataStorageSnapshot UChunkLoadScheduler::GetMetadataStorageSnapshot() const
{
  MetadataStorageSnapshot snapshot{MetadataMemory.GetSnapshot(),0};
  // The ordered index shares each authoritative request's immutable settings.
  for(const auto &entry:QueuedRequests)
    snapshot.pending_string_capacity_bytes+=SettingsStringCharge(entry.second.settings->value);
  return snapshot;
}

size_t UChunkLoadScheduler::RetireCommittedOutsideInterest(glm::ivec3 center,int radius,
    const std::function<bool(glm::ivec3)> &has_owner,size_t budget)
{
  const size_t count=std::min({budget,size_t{32},States.size()});
  if(count==0) return 0;
  auto it=MetadataRetirementCursor ? States.upper_bound(*MetadataRetirementCursor) : States.begin();
  if(it==States.end()) it=States.begin();
  std::vector<glm::ivec3> keys; keys.reserve(count);
  for(size_t n=0;n<count;++n) {
    keys.push_back(it->first); MetadataRetirementCursor=it->first;
    if(++it==States.end()) it=States.begin();
  }
  size_t retired=0;
  for(const auto key:keys) {
    const auto state=States.find(key);
    if(state==States.end() || state->second.state!=ChunkLoadState::Committed) continue;
    const int64_t distance=std::max(std::abs(int64_t(key.x)-center.x),std::abs(int64_t(key.z)-center.z));
    if(distance<=std::max(0,radius) || (has_owner && has_owner(key))) continue;
    Cancel(key,"abandoned_interest"); ++retired;
  }
  return retired;
}

bool UChunkLoadScheduler::IsCommitted(glm::ivec3 coord) const
{
  const auto it = States.find(coord);
  return it != States.end() && it->second.state == ChunkLoadState::Committed;
}

bool UChunkLoadScheduler::IsPending(glm::ivec3 coord) const
{
  const auto it = States.find(coord);
  if (it == States.end())
  {
    return false;
  }
  return it->second.state != ChunkLoadState::Absent &&
         it->second.state != ChunkLoadState::Committed;
}

ChunkLoadState UChunkLoadScheduler::GetState(glm::ivec3 coord) const
{
  const auto it = States.find(coord);
  if (it == States.end())
  {
    return ChunkLoadState::Absent;
  }
  return it->second.state;
}

int UChunkLoadScheduler::GetPendingQueueCount() const
{
  return static_cast<int>(Queue.size());
}

int UChunkLoadScheduler::GetGenInFlightCount() const
{
  return static_cast<int>(Pool.GetActiveJobCount());
}

int UChunkLoadScheduler::GetCompletedReadyCount() const
{
  return static_cast<int>(Completed.Size());
}

size_t UChunkLoadScheduler::GetCompletedReadyPayloadBytes() const
{
  size_t bytes=0;
  Completed.Any([&](const PendingResult &pending) {
    bytes+=pending.result.buffer.GetOwnedAllocationBytes();
    return false;
  });
  return bytes;
}

int UChunkLoadScheduler::GetGenBacklogTotal() const
{
  return GetPendingQueueCount() + GetGenInFlightCount() + GetCompletedReadyCount();
}

} // namespace cutum
