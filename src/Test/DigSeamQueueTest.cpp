#include "World/Mesh/DigSeamQueue.h"

#include <cstdlib>
#include <iostream>

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

} // namespace

int main()
{
  using cutum::DigSeamQueue;

  {
    DigSeamQueue q;
    q.Enqueue({1, 0, 0});
    q.Enqueue({1, 0, 0});
    Expect(q.Size() == 1, "dedupe same coord");
  }

  {
    DigSeamQueue q;
    for (int i = 0; i < 12; ++i)
    {
      q.Enqueue({i, 0, 0});
    }
    Expect(q.Size() == DigSeamQueue::kCap, "cap at 8");
    glm::ivec3 first{};
    Expect(q.TryPop(first), "pop oldest");
    Expect(first == glm::ivec3(4, 0, 0), "FIFO drop oldest on overflow");
  }

  {
    DigSeamQueue q;
    q.Enqueue({2, 1, 0});
    q.Enqueue({3, 1, 0});
    Expect(q.Size() == 2, "two pending");
    glm::ivec3 a{};
    Expect(q.TryPop(a) && a == glm::ivec3(2, 1, 0), "pop front");
    Expect(q.Size() == 1, "one left");
    Expect(!q.Empty(), "not empty");
  }

  if (gFails != 0)
  {
    std::cerr << gFails << " DigSeamQueueTest failure(s)\n";
    return EXIT_FAILURE;
  }
  std::cout << "DigSeamQueueTest OK\n";
  return EXIT_SUCCESS;
}
