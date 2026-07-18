#include "Render/Mesh/ChunkDirtySet.h"

#include <glm/glm.hpp>
#include <cstdlib>
#include <iostream>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "world_mesh_service_test: " << message << std::endl;
    std::exit(1);
  }
}

int main()
{
  cutum::UChunkDirtySet dirty;

  dirty.MarkDirty(glm::ivec3{0, 0, 0});
  Expect(dirty.GetCount() == 1, "first MarkDirty should add one entry");

  dirty.MarkDirty(glm::ivec3{0, 0, 0});
  Expect(dirty.GetCount() == 1, "duplicate MarkDirty should dedup");

  dirty.MarkDirty(glm::ivec3{1, 0, 0});
  Expect(dirty.GetCount() == 2, "second chunk should increase dirty count");

  dirty.Erase(glm::ivec3{0, 0, 0});
  Expect(dirty.GetCount() == 1,
          "Erase should remove coord from dirty queue (RemoveChunk uses this)");

  // Vertical priority: after near/far split, surface cy before deep when above water.
  dirty.Clear();
  dirty.MarkDirty(glm::ivec3{0, 0, 0});
  dirty.MarkDirty(glm::ivec3{0, 3, 0});
  dirty.MarkDirty(glm::ivec3{0, 1, 0});
  dirty.PrioritizeNearHorizontal(glm::ivec3{0, 0, 0}, 2);
  dirty.PrioritizeVerticalCy(glm::ivec3{0, 0, 0}, 2, /*preferred_cy=*/3,
                             /*prefer_lower_cy=*/false);
  Expect(dirty.begin()->y == 3, "above water: preferred cy should drain first");

  dirty.Clear();
  dirty.MarkDirty(glm::ivec3{0, 3, 0});
  dirty.MarkDirty(glm::ivec3{0, 0, 0});
  dirty.MarkDirty(glm::ivec3{0, 1, 0});
  dirty.PrioritizeNearHorizontal(glm::ivec3{0, 0, 0}, 2);
  dirty.PrioritizeVerticalCy(glm::ivec3{0, 0, 0}, 2, /*preferred_cy=*/3,
                             /*prefer_lower_cy=*/true);
  Expect(dirty.begin()->y == 0, "underwater: lower cy should drain first");

  std::cout << "world_mesh_service_test: OK" << std::endl;
  return 0;
}
