#include "World/Physics/ChunkRebuildQueue.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "chunk_rebuild_queue_test: " << message << std::endl;
    std::exit(1);
  }
}

int main()
{
  cutum::UChunkRebuildQueue queue;
  queue.SetLimits(2, 1, 8);

  Expect(queue.Enqueue(glm::ivec3(0, 0, 0), 0, 1),
         "near chunk should enqueue at soft limit");
  Expect(!queue.Enqueue(glm::ivec3(1, 0, 0), 5, 2),
         "far chunk should defer at soft limit");

  cutum::UChunkRebuildQueue priorityQueue;
  priorityQueue.SetLimits(4, 64, 128);
  Expect(priorityQueue.Enqueue(glm::ivec3(2, 0, 0), 3, 2), "enqueue far");
  Expect(priorityQueue.Enqueue(glm::ivec3(0, 0, 0), 0, 1), "enqueue near");

  const std::vector<glm::ivec3> popped = priorityQueue.PopBudgeted();
  Expect(popped.size() == 2, "expected two chunks in budget");
  Expect(popped[0] == glm::ivec3(0, 0, 0),
         "near-player chunk must be processed first");

  cutum::UChunkRebuildQueue timed;
  timed.SetLimits(4, 64, 128);
  for (int i = 0; i < 5; ++i)
    timed.Enqueue({0, -3, i}, 0, i);
  int applied = 0;
  Expect(timed.DrainBudgeted([&](glm::ivec3) { ++applied; },
                             [] { return false; }) == 0 && timed.Size() == 5 &&
             timed.GetStats().Processed == 0,
         "already exhausted deadline removes no request or processed credit");
  timed.DrainBudgeted([&](glm::ivec3) { ++applied; },
                      [&] { return applied < 2; });
  Expect(applied == 2 && timed.Size() == 3 &&
             timed.GetStats().Processed == 2,
         "deadline must retain the entire unprocessed visual/save suffix");
  Expect(!timed.Enqueue({0, -3, 2}, 0, 99),
         "pending tail retains coalescing membership");
  Expect(timed.Enqueue({0, -3, 5}, 0, 5), "new arrival joins retained tail");
  const auto tail = timed.PopBudgeted();
  Expect(tail == std::vector<glm::ivec3>({{0,-3,2}, {0,-3,3}, {0,-3,4}, {0,-3,5}}) &&
             timed.Size() == 0 && timed.GetStats().Processed == 6 &&
             timed.GetStats().Dropped == 0,
         "retained suffix and new arrival complete in original priority/order");
  timed.SetLimits(0, 64, 128);
  timed.Enqueue({0,-3,6}, 0, 6);
  Expect(timed.DrainBudgeted([](glm::ivec3) {}, [] { return true; }) == 0 &&
             timed.Size() == 1, "zero item budget retains all work");
  timed.SetLimits(4, 64, 128);
  bool threw = false;
  try
  {
    timed.DrainBudgeted([](glm::ivec3) { throw std::runtime_error("consumer"); },
                        [] { return true; });
  }
  catch (const std::runtime_error &) { threw = true; }
  Expect(threw && timed.Size() == 1 && timed.GetStats().Processed == 6,
         "failed consumer retains its request and does not claim processing");
  Expect(timed.PopBudgeted() == std::vector<glm::ivec3>({{0,-3,6}}),
         "failed consumer request remains retryable");

  std::cout << "chunk_rebuild_queue_test: OK" << std::endl;
  return 0;
}
