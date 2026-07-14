#include "World/Chunks/ChunkLoadPriority.h"

#include <glm/glm.hpp>
#include <cassert>
#include <cstdlib>
#include <iostream>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "chunk_load_priority_test: " << message << std::endl;
    std::exit(1);
  }
}

int main()
{
  const cutum::ChunkLoadPriorityParams params;
  const glm::ivec3 feet(0, 0, 0);
  const glm::vec3 forward(0.0f, 0.0f, 1.0f);

  const int ahead = cutum::ComputeChunkLoadPriority(glm::ivec3(0, 0, 2), feet,
                                                    forward, params);
  const int side = cutum::ComputeChunkLoadPriority(glm::ivec3(2, 0, 0), feet,
                                                   forward, params);
  Expect(ahead < side, "forward chunk should outrank side chunk at same ring");

  const int feet_chunk = cutum::ComputeChunkLoadPriority(glm::ivec3(0, 0, 0),
                                                         feet, forward, params);
  Expect(feet_chunk < ahead, "feet neighborhood should outrank distant chunks");

  std::cout << "chunk_load_priority_test: OK" << std::endl;
  return 0;
}
