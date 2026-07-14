#include "World/Chunks/ChunkGenerationToken.h"
#include <cassert>
#include <glm/glm.hpp>
#include <thread>
#include <vector>

int main()
{
  cutum::UChunkGenerationRegistry registry;
  const glm::ivec3 coord(3, 0, -2);

  const uint64_t first = registry.Bump(coord);
  assert(first >= 1);
  assert(registry.Current(coord).sequence == first);

  std::vector<std::thread> workers;
  workers.reserve(8);
  for (int i = 0; i < 8; ++i)
  {
    workers.emplace_back(
        [&registry, coord]()
        {
          for (int n = 0; n < 1000; ++n)
          {
            (void)registry.Current(coord);
          }
        });
  }
  for (int i = 0; i < 4; ++i)
  {
    registry.Bump(coord);
  }
  for (std::thread &worker : workers)
  {
    worker.join();
  }

  const uint64_t final_seq = registry.Current(coord).sequence;
  assert(final_seq == first + 4);
  return 0;
}
