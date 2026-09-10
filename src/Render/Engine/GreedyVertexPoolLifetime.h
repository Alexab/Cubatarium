#ifndef GREEDYVERTEXPOOLLIFETIME_H
#define GREEDYVERTEXPOOLLIFETIME_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace cutum
{

struct GreedyGpuPoolFreeSlot
{
  size_t vertexByteOffset{0};
  size_t indexByteOffset{0};
  size_t vertexBytes{0};
  size_t indexBytes{0};
};

enum class VertexPoolFencePoll : uint8_t
{
  Signaled = 0,
  Pending = 1,
  Failed = 2,
};

struct VertexPoolRetiredSlot
{
  GreedyGpuPoolFreeSlot slot;
  uint64_t fenceToken{0};
};

/// CPU-side retire → poll → reclaim queue (injectable fence poll for tests).
class VertexPoolRetireQueue
{
public:
  using PollFenceFn =
      std::function<VertexPoolFencePoll(uint64_t fenceToken)>;

  /// Free-time path: slot waits for draw fence (not tied to Free-time token).
  void FreePending(GreedyGpuPoolFreeSlot slot);
  /// After draw: attach draw-generation fence to all pending slots.
  void SignalDrawComplete(uint64_t fenceToken);
  /// Free after draw in same frame — retire with active draw fence.
  void Retire(GreedyGpuPoolFreeSlot slot, uint64_t fenceToken);
  void PollRetired(const PollFenceFn &poll);
  void ReclaimToFreeList(std::vector<GreedyGpuPoolFreeSlot> &freeList);

  size_t RetiredCount() const { return Retired_.size(); }
  size_t PendingCount() const { return Pending_.size(); }
  size_t PendingFenceCount() const { return Retired_.size(); }
  /// Reserve bump reset is safe only when no retired/pending slots remain.
  bool CanBumpReset() const { return Retired_.empty() && Pending_.empty(); }

private:
  std::vector<GreedyGpuPoolFreeSlot> Pending_;
  std::vector<VertexPoolRetiredSlot> Retired_;
  std::vector<GreedyGpuPoolFreeSlot> Reclaimed_;
};

} // namespace cutum

#endif
