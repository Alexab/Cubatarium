#include "World/Physics/ChunkRebuildQueue.h"

#include <cstdlib>
#include <iostream>

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

  std::cout << "chunk_rebuild_queue_test: OK" << std::endl;
  return 0;
}
