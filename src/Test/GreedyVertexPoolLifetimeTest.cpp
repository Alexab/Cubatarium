#include "Render/Engine/GreedyVertexPoolLifetime.h"

#include <cstdint>
#include <iostream>
#include <unordered_map>

namespace
{

int gFails = 0;

void Expect(bool cond, const char *msg)
{
  if (!cond)
  {
    std::cerr << "FAIL: " << msg << "\n";
    ++gFails;
  }
}

enum class MockFenceState : uint8_t
{
  Pending = 0,
  Signaled = 1,
  Failed = 2,
};

std::unordered_map<uint64_t, MockFenceState> gFenceStates;

cutum::VertexPoolFencePoll MockPoll(uint64_t token)
{
  const auto it = gFenceStates.find(token);
  if (it == gFenceStates.end())
  {
    return cutum::VertexPoolFencePoll::Failed;
  }
  switch (it->second)
  {
  case MockFenceState::Signaled:
    return cutum::VertexPoolFencePoll::Signaled;
  case MockFenceState::Pending:
    return cutum::VertexPoolFencePoll::Pending;
  case MockFenceState::Failed:
    return cutum::VertexPoolFencePoll::Failed;
  }
  return cutum::VertexPoolFencePoll::Failed;
}

cutum::GreedyGpuPoolFreeSlot MakeSlot(size_t vOff, size_t iOff, size_t vBytes,
                                      size_t iBytes)
{
  cutum::GreedyGpuPoolFreeSlot slot;
  slot.vertexByteOffset = vOff;
  slot.indexByteOffset = iOff;
  slot.vertexBytes = vBytes;
  slot.indexBytes = iBytes;
  return slot;
}

void TestRetirePollReclaim()
{
  cutum::VertexPoolRetireQueue queue;
  gFenceStates.clear();
  gFenceStates[1] = MockFenceState::Pending;
  gFenceStates[2] = MockFenceState::Signaled;

  queue.Retire(MakeSlot(0, 0, 64, 32), 1);
  queue.Retire(MakeSlot(64, 32, 64, 32), 2);

  queue.PollRetired(MockPoll);
  Expect(queue.RetiredCount() == 1, "pending fence stays retired");

  std::vector<cutum::GreedyGpuPoolFreeSlot> freeList;
  queue.ReclaimToFreeList(freeList);
  Expect(freeList.size() == 1, "signaled slot reclaimed");
  Expect(freeList[0].vertexByteOffset == 64, "reclaimed offset");

  gFenceStates[1] = MockFenceState::Signaled;
  queue.PollRetired(MockPoll);
  Expect(queue.RetiredCount() == 0, "all retired cleared");
  queue.ReclaimToFreeList(freeList);
  Expect(freeList.size() == 2, "second slot reclaimed after signal");
}

void TestFailedFenceNeverReclaims()
{
  cutum::VertexPoolRetireQueue queue;
  gFenceStates.clear();
  gFenceStates[9] = MockFenceState::Failed;

  queue.Retire(MakeSlot(128, 64, 32, 16), 9);
  queue.PollRetired(MockPoll);
  Expect(queue.RetiredCount() == 1, "failed fence kept retired");

  std::vector<cutum::GreedyGpuPoolFreeSlot> freeList;
  queue.ReclaimToFreeList(freeList);
  Expect(freeList.empty(), "failed fence not reclaimed");
}

void TestDrawGenerationRetireNotFreeTime()
{
  cutum::VertexPoolRetireQueue queue;
  gFenceStates.clear();
  gFenceStates[10] = MockFenceState::Pending;

  queue.FreePending(MakeSlot(0, 0, 128, 64));
  Expect(queue.PendingCount() == 1, "free-time slot waits for draw fence");
  Expect(queue.RetiredCount() == 0, "no retire token at free-time");

  queue.SignalDrawComplete(10);
  Expect(queue.PendingCount() == 0, "draw attaches fence to pending retires");
  Expect(queue.RetiredCount() == 1, "pending moved to retired with draw token");
  Expect(!queue.CanBumpReset(), "reserve blocked while retired pending");

  queue.PollRetired(MockPoll);
  Expect(queue.RetiredCount() == 1, "draw fence still pending");

  gFenceStates[10] = MockFenceState::Signaled;
  queue.PollRetired(MockPoll);
  Expect(queue.RetiredCount() == 0, "signaled draw generation reclaimed");
  Expect(queue.CanBumpReset(), "reserve allowed after reclaim");
}

void TestReserveBlockedWhileRetiredPending()
{
  cutum::VertexPoolRetireQueue queue;
  gFenceStates.clear();
  gFenceStates[20] = MockFenceState::Pending;

  queue.FreePending(MakeSlot(256, 128, 64, 32));
  queue.SignalDrawComplete(20);
  Expect(!queue.CanBumpReset(), "bump reset blocked with retired pending");

  gFenceStates[20] = MockFenceState::Signaled;
  queue.PollRetired(MockPoll);
  std::vector<cutum::GreedyGpuPoolFreeSlot> freeList;
  queue.ReclaimToFreeList(freeList);
  Expect(queue.CanBumpReset(), "bump reset safe after poll + reclaim");
  Expect(freeList.size() == 1, "retired slot returned to free list");
}

} // namespace

int main()
{
  TestRetirePollReclaim();
  TestFailedFenceNeverReclaims();
  TestDrawGenerationRetireNotFreeTime();
  TestReserveBlockedWhileRetiredPending();

  if (gFails != 0)
  {
    std::cerr << "greedy_vertex_pool_lifetime_test: " << gFails
              << " failures\n";
    return 1;
  }
  std::cout << "greedy_vertex_pool_lifetime_test: ok\n";
  return 0;
}
