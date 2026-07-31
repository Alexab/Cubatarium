#include "World/Chunks/ChunkLoadScheduler.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/BlockWorld.h"
#include "Core/Jobs/JobThreadBudget.h"
#include "WorldGen/Core/WorldGenContentPin.h"
#include <algorithm>
#include <chrono>
#include <thread>

namespace cutum
{

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
      const auto prioIt = RequestPriorities.find(coord);
      if (prioIt != RequestPriorities.end() && priority >= prioIt->second)
      {
        return;
      }
    }
    else if (stateIt->second == ChunkLoadState::Generating ||
             stateIt->second == ChunkLoadState::Ready)
    {
      // Job already running / ready: refresh live priority so commit order
      // tracks the player even if the column was started far away.
      const auto prioIt = RequestPriorities.find(coord);
      if (prioIt != RequestPriorities.end() && priority >= prioIt->second)
      {
        return;
      }
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
  pending.settings = settings;
  pending.columnOrigin = column_origin;
  pending.hasColumnOrigin = has_column_origin;
  States[coord] = ChunkLoadState::Requested;
  ActiveTokens[coord] = pending.token;
  RequestPriorities[coord] = priority;
  Queue.push(pending);
}

void UChunkLoadScheduler::Cancel(glm::ivec3 coord)
{
  States.erase(coord);
  ActiveTokens.erase(coord);
  RequestPriorities.erase(coord);
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
  const int priority = request.priority;
  const int max_height = request.settings.MaxHeight;
  Pool.Enqueue(
      [this, populateRequest, priority, max_height]()
      {
        PendingResult pending;
        pending.priority = priority;
        pending.maxHeight = max_height;
        pending.result = Populator.Populate(populateRequest);
        Completed.Push(std::move(pending));
      });
}

void UChunkLoadScheduler::Tick(UBlockWorld &world, int maxCommitsPerFrame,
                               int maxGenerationStartsPerFrame)
{
  LastTickApplyMs = 0.0;
  int generationStarts = 0;
  while (!Queue.empty() && generationStarts < maxGenerationStartsPerFrame)
  {
    const PendingRequest next = Queue.top();
    Queue.pop();
    if (States[next.coord] != ChunkLoadState::Requested)
    {
      continue;
    }
    const auto prioIt = RequestPriorities.find(next.coord);
    if (prioIt != RequestPriorities.end() && next.priority > prioIt->second)
    {
      continue;
    }
    ScheduleWorker(next);
    ++generationStarts;
  }

  std::vector<PendingResult> ready = Completed.DrainAll();
  std::sort(ready.begin(), ready.end(),
            [this](const PendingResult &a, const PendingResult &b)
            {
              auto live_priority = [this](const PendingResult &pending) -> int
              {
                const auto it = RequestPriorities.find(pending.result.coord);
                if (it != RequestPriorities.end())
                {
                  return std::min(pending.priority, it->second);
                }
                return pending.priority;
              };
              return live_priority(a) < live_priority(b);
            });
  int committed = 0;
  for (PendingResult &pending : ready)
  {
    States[pending.result.coord] = ChunkLoadState::Ready;
    const auto tokenIt = ActiveTokens.find(pending.result.coord);
    if (tokenIt == ActiveTokens.end() ||
        !pending.result.token.IsValidFor(pending.result.coord,
                                         tokenIt->second.sequence) ||
        pending.result.discarded)
    {
      States.erase(pending.result.coord);
      RequestPriorities.erase(pending.result.coord);
      continue;
    }
    if (committed >= maxCommitsPerFrame)
    {
      Completed.Push(std::move(pending));
      continue;
    }
    const auto apply_t0 = std::chrono::high_resolution_clock::now();
    pending.result.buffer.ApplyTo(world);
    States[pending.result.coord] = ChunkLoadState::Committed;
    ActiveTokens.erase(pending.result.coord);
    RequestPriorities.erase(pending.result.coord);
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
    LastTickApplyMs += std::chrono::duration<double, std::milli>(
                           std::chrono::high_resolution_clock::now() - apply_t0)
                           .count();
    ++committed;
  }
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
