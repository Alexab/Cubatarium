#include "Render/Engine/GreedyVertexPoolLifetime.h"

namespace cutum
{

void VertexPoolRetireQueue::FreePending(GreedyGpuPoolFreeSlot slot)
{
  Pending_.push_back(slot);
}

void VertexPoolRetireQueue::SignalDrawComplete(uint64_t fenceToken)
{
  for (const GreedyGpuPoolFreeSlot &slot : Pending_)
  {
    VertexPoolRetiredSlot retired;
    retired.slot = slot;
    retired.fenceToken = fenceToken;
    Retired_.push_back(retired);
  }
  Pending_.clear();
}

void VertexPoolRetireQueue::Retire(GreedyGpuPoolFreeSlot slot,
                                   uint64_t fenceToken)
{
  VertexPoolRetiredSlot retired;
  retired.slot = slot;
  retired.fenceToken = fenceToken;
  Retired_.push_back(retired);
}

void VertexPoolRetireQueue::PollRetired(const PollFenceFn &poll)
{
  if (Retired_.empty() || !poll)
  {
    return;
  }
  size_t write = 0;
  for (size_t i = 0; i < Retired_.size(); ++i)
  {
    VertexPoolRetiredSlot &entry = Retired_[i];
    const VertexPoolFencePoll result = poll(entry.fenceToken);
    if (result == VertexPoolFencePoll::Signaled)
    {
      Reclaimed_.push_back(entry.slot);
      continue;
    }
    if (result == VertexPoolFencePoll::Pending ||
        result == VertexPoolFencePoll::Failed)
    {
      if (write != i)
      {
        Retired_[write] = entry;
      }
      ++write;
    }
  }
  Retired_.resize(write);
}

void VertexPoolRetireQueue::ReclaimToFreeList(
    std::vector<GreedyGpuPoolFreeSlot> &freeList)
{
  if (Reclaimed_.empty())
  {
    return;
  }
  freeList.insert(freeList.end(), Reclaimed_.begin(), Reclaimed_.end());
  Reclaimed_.clear();
}

} // namespace cutum
