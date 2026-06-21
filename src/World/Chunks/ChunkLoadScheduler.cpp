#include "World/Chunks/ChunkLoadScheduler.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/BlockWorld.h"

namespace cutum
{

UChunkLoadScheduler::UChunkLoadScheduler(IChunkPopulator &populator,
                                         UChunkGenerationRegistry &tokens)
    : Populator(populator), Tokens(tokens)
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
                                      const ProceduralSettings &settings)
{
  if (coord.y != 0)
  {
    return;
  }
  const auto stateIt = States.find(coord);
  if (stateIt != States.end())
  {
    if (stateIt->second != ChunkLoadState::Absent)
    {
      return;
    }
  }
  PendingRequest pending;
  pending.coord = coord;
  pending.priority = priority;
  pending.token = Tokens.Current(coord);
  pending.settings = settings;
  States[coord] = ChunkLoadState::Requested;
  ActiveTokens[coord] = pending.token;
  Queue.push(pending);
}

void UChunkLoadScheduler::Cancel(glm::ivec3 coord)
{
  States.erase(coord);
  ActiveTokens.erase(coord);
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
  Pool.Enqueue(
      [this, populateRequest]()
      {
        PendingResult pending;
        pending.result = Populator.Populate(populateRequest);
        Completed.Push(std::move(pending));
      });
}

void UChunkLoadScheduler::Tick(UBlockWorld &world, int maxCommitsPerFrame)
{
  while (!Queue.empty())
  {
    const PendingRequest next = Queue.top();
    Queue.pop();
    if (States[next.coord] == ChunkLoadState::Requested)
    {
      ScheduleWorker(next);
    }
  }

  std::vector<PendingResult> ready = Completed.DrainAll();
  int committed = 0;
  for (PendingResult &pending : ready)
  {
    States[pending.result.coord] = ChunkLoadState::Ready;
    const auto tokenIt = ActiveTokens.find(pending.result.coord);
    if (tokenIt == ActiveTokens.end() ||
        !pending.result.token.IsValidFor(pending.result.coord,
                                         tokenIt->second.sequence))
    {
      States.erase(pending.result.coord);
      continue;
    }
    if (committed >= maxCommitsPerFrame)
    {
      Completed.Push(std::move(pending));
      continue;
    }
    pending.result.buffer.ApplyTo(world);
    States[pending.result.coord] = ChunkLoadState::Committed;
    ActiveTokens.erase(pending.result.coord);
    if (ColumnMeshDirty && pending.result.buffer.HasYBounds())
    {
      ColumnMeshDirty(pending.result.coord, pending.result.buffer.GetMinY(),
                      pending.result.buffer.GetMaxY());
    }
    if (MarkDirty)
    {
      MarkDirty(pending.result.coord);
    }
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

} // namespace cutum
