#include "World/Chunks/ChunkLoadScheduler.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/BlockWorld.h"
#include "App/Platform/Log.h"
#include "Core/Jobs/JobThreadBudget.h"
#include "WorldGen/Core/WorldGenContentPin.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <thread>

namespace cutum
{

namespace
{

bool IsWorldColumnSourceTraceEnabled()
{
  const char *value = std::getenv("CUBA_WORLD_COLUMN_SOURCE_TRACE");
  return value && value[0] == '1';
}

} // namespace

UChunkLoadScheduler::UChunkLoadScheduler(IUChunkPopulator &populator,
                                         UChunkGenerationRegistry &tokens)
    : Populator(populator), Tokens(tokens),
      Pool(ComputeWorkerThreadCount(JobPoolKind::ChunkGeneration),
           "ChunkGeneration")
{
}

void UChunkLoadScheduler::SetMarkDirtyFn(MarkChunkDirtyFn fn)
{
  MarkDirty = std::move(fn);
}

void UChunkLoadScheduler::SetColumnMeshDirtyFn(ColumnMeshDirtyFn fn)
{
  ColumnMeshDirty = std::move(fn);
}

void UChunkLoadScheduler::RequestLoad(glm::ivec3 coord, int priority,
                                      const ProceduralSettings &settings,
                                      glm::ivec2 column_origin,
                                      bool has_column_origin)
{
  if (coord.y != 0)
  {
    return;
  }
  const auto stateIt = States.find(coord);
  if (stateIt != States.end())
  {
    if (stateIt->second == ChunkLoadState::Requested)
    {
      const auto queuedIt = QueuedRequests.find(coord);
      if (queuedIt != QueuedRequests.end())
      {
        if (priority == queuedIt->second.priority)
        {
          return;
        }
        // Re-rank in both directions as the focus moves. Preserve the original
        // request time/settings; resetting them would hide queue wait and make
        // generated columns depend on which camera update happened last.
        PendingRequest updated = queuedIt->second;
        updated.priority = priority;
        updated.queueRevision = NextRequestQueueRevision++;
        queuedIt->second = updated;
        RequestPriorities[coord] = priority;
        ++RequestPriorityRefreshCounts[coord];
        Queue.push(std::move(updated));
        CompactRequestQueueIfStale();
        return;
      }
    }
    else if (stateIt->second == ChunkLoadState::Generating ||
             stateIt->second == ChunkLoadState::Ready)
    {
      // Job already running / ready: refresh live priority in either direction
      // so stale near-focus work cannot retain its old commit preference.
      const auto prioIt = RequestPriorities.find(coord);
      if (prioIt != RequestPriorities.end() && priority == prioIt->second)
      {
        return;
      }
      if (InitialRequestPriorities.find(coord) ==
          InitialRequestPriorities.end())
      {
        InitialRequestPriorities[coord] =
            prioIt != RequestPriorities.end() ? prioIt->second : priority;
      }
      ++RequestPriorityRefreshCounts[coord];
      RequestPriorities[coord] = priority;
      return;
    }
    else if (stateIt->second != ChunkLoadState::Absent)
    {
      return;
    }
  }
  PendingRequest pending;
  pending.coord = coord;
  pending.priority = priority;
  pending.token = Tokens.Current(coord);
  pending.requestedAt = std::chrono::steady_clock::now();
  pending.queueRevision = NextRequestQueueRevision++;
  pending.settings = settings;
  pending.columnOrigin = column_origin;
  pending.hasColumnOrigin = has_column_origin;
  States[coord] = ChunkLoadState::Requested;
  ActiveTokens[coord] = pending.token;
  RequestPriorities[coord] = priority;
  InitialRequestPriorities[coord] = priority;
  RequestPriorityRefreshCounts[coord] = 0;
  QueuedRequests[coord] = pending;
  Queue.push(std::move(pending));
  CompactRequestQueueIfStale();
}

void UChunkLoadScheduler::Cancel(glm::ivec3 coord)
{
  States.erase(coord);
  ActiveTokens.erase(coord);
  RequestPriorities.erase(coord);
  InitialRequestPriorities.erase(coord);
  RequestPriorityRefreshCounts.erase(coord);
  QueuedRequests.erase(coord);
  CompactRequestQueueIfStale();
}

void UChunkLoadScheduler::CompactRequestQueueIfStale()
{
  // Priority refreshes leave obsolete heap nodes behind. Compact only after
  // stale nodes exceed the live set by a wide margin, bounding memory and the
  // number of stale pops performed in one frame.
  constexpr std::size_t kStaleQueueSlack = 64;
  const std::size_t live = QueuedRequests.size();
  if (!Queue.empty() &&
      (live == 0 || Queue.size() > live * 2 + kStaleQueueSlack))
  {
    std::priority_queue<PendingRequest, std::vector<PendingRequest>,
                        RequestCompare>
        compacted;
    for (const auto &entry : QueuedRequests)
    {
      compacted.push(entry.second);
    }
    Queue.swap(compacted);
  }
}

void UChunkLoadScheduler::CancelAllPending(
    const std::chrono::milliseconds worker_wait)
{
  Queue = std::priority_queue<PendingRequest, std::vector<PendingRequest>,
                              RequestCompare>();
  std::vector<glm::ivec3> bump_coords;
  bump_coords.reserve(ActiveTokens.size() + States.size());
  for (const auto &entry : ActiveTokens)
  {
    bump_coords.push_back(entry.first);
  }
  for (const auto &entry : States)
  {
    bump_coords.push_back(entry.first);
  }
  for (const glm::ivec3 &coord : bump_coords)
  {
    Tokens.Bump(coord);
  }
  States.clear();
  ActiveTokens.clear();
  RequestPriorities.clear();
  InitialRequestPriorities.clear();
  RequestPriorityRefreshCounts.clear();
  QueuedRequests.clear();
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
  CancelAllPending(std::chrono::milliseconds(0));
  Pool.ShutdownForProcessExit(timeout);
  (void)Completed.DrainAll();
}

void UChunkLoadScheduler::Invalidate(glm::ivec3 coord)
{
  Tokens.Bump(coord);
  Cancel(coord);
}

void UChunkLoadScheduler::ScheduleWorker(const PendingRequest &request)
{
  States[request.coord] = ChunkLoadState::Generating;
  ChunkPopulateRequest populateRequest;
  populateRequest.chunkCoord = request.coord;
  populateRequest.token = request.token;
  populateRequest.settings = request.settings;
  populateRequest.columnOrigin = request.columnOrigin;
  populateRequest.hasColumnOrigin = request.hasColumnOrigin;
  const glm::ivec3 coord = request.coord;
  const uint64_t start_sequence = request.token.sequence;
  populateRequest.shouldCancel = [this, coord, start_sequence]()
  { return Tokens.Current(coord).sequence != start_sequence; };
  populateRequest.content = CaptureWorldGenContentSnapshot();
  const auto requested_at = request.requestedAt;
  const int priority = request.priority;
  const int max_height = request.settings.MaxHeight;
  Pool.Enqueue(
      [this, populateRequest, requested_at, priority, max_height]()
      {
        PendingResult pending;
        pending.priority = priority;
        pending.maxHeight = max_height;
        pending.requestedAt = requested_at;
        pending.generationStartedAt = std::chrono::steady_clock::now();
        const auto generation_started = pending.generationStartedAt;
        pending.result = Populator.Populate(populateRequest);
        pending.generationFinishedAt = std::chrono::steady_clock::now();
        pending.generationMs = std::chrono::duration<double, std::milli>(
                                   pending.generationFinishedAt -
                                   generation_started)
                                   .count();
        Completed.Push(std::move(pending));
      });
}

void UChunkLoadScheduler::Tick(UBlockWorld &world, int maxCommitsPerFrame,
                               int maxGenerationStartsPerFrame,
                               double maxApplyMsPerFrame,
                               bool allowBoundedReadyDrain)
{
  LastTickApplyMs = 0.0;
  LastCommitsThisFrame = 0;
  int generationStarts = 0;
  while (!Queue.empty() && generationStarts < maxGenerationStartsPerFrame)
  {
    const PendingRequest next = Queue.top();
    Queue.pop();
    const auto stateIt = States.find(next.coord);
    const auto queuedIt = QueuedRequests.find(next.coord);
    if (stateIt == States.end() ||
        stateIt->second != ChunkLoadState::Requested ||
        queuedIt == QueuedRequests.end() ||
        queuedIt->second.queueRevision != next.queueRevision)
    {
      continue;
    }
    const auto prioIt = RequestPriorities.find(next.coord);
    if (prioIt != RequestPriorities.end() && next.priority != prioIt->second)
    {
      continue;
    }
    ScheduleWorker(next);
    QueuedRequests.erase(queuedIt);
    ++generationStarts;
  }

  std::vector<PendingResult> ready = Completed.DrainAll();
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
      continue;
    }
    if (pending.result.discarded)
    {
      States.erase(pending.result.coord);
      ActiveTokens.erase(tokenIt);
      RequestPriorities.erase(pending.result.coord);
      InitialRequestPriorities.erase(pending.result.coord);
      RequestPriorityRefreshCounts.erase(pending.result.coord);
      continue;
    }
    States[pending.result.coord] = ChunkLoadState::Ready;
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
    States[pending.result.coord] = ChunkLoadState::Committed;
    ActiveTokens.erase(pending.result.coord);
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
      const double total_ms = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() -
                                  pending.requestedAt)
                                  .count();
      const std::string message =
          "source=procedural outcome=committed coord=(" +
          std::to_string(pending.result.coord.x) + ",0," +
          std::to_string(pending.result.coord.z) + ") priority=" +
          std::to_string(pending.priority) + " token=" +
          std::to_string(pending.result.token.sequence) + " queue_ms=" +
          std::to_string(queue_ms) + " generation_ms=" +
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

bool UChunkLoadScheduler::IsCommitted(glm::ivec3 coord) const
{
  const auto it = States.find(coord);
  return it != States.end() && it->second == ChunkLoadState::Committed;
}

bool UChunkLoadScheduler::IsPending(glm::ivec3 coord) const
{
  const auto it = States.find(coord);
  if (it == States.end())
  {
    return false;
  }
  return it->second != ChunkLoadState::Absent &&
         it->second != ChunkLoadState::Committed;
}

ChunkLoadState UChunkLoadScheduler::GetState(glm::ivec3 coord) const
{
  const auto it = States.find(coord);
  if (it == States.end())
  {
    return ChunkLoadState::Absent;
  }
  return it->second;
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

int UChunkLoadScheduler::GetGenBacklogTotal() const
{
  return GetPendingQueueCount() + GetGenInFlightCount() + GetCompletedReadyCount();
}

} // namespace cutum
