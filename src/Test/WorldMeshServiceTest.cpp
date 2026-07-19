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

  // Distance key: nearer horiz wins over farther missing holes.
  dirty.Clear();
  dirty.MarkDirty(glm::ivec3{3, 0, 0}); // far missing
  dirty.MarkDirty(glm::ivec3{0, 2, 0}); // near remesh
  dirty.MarkDirty(glm::ivec3{0, 0, 0}); // near missing
  dirty.MarkDirty(glm::ivec3{1, 0, 0}); // dist 1
  auto missing = [](glm::ivec3 c)
  { return c.y == 0; };
  dirty.SortByDistanceKey(glm::ivec3{0, 0, 0}, /*preferred_cy=*/0,
                          /*prefer_lower_cy=*/false, /*vertical_valid=*/true,
                          missing);
  Expect(dirty.begin()->x == 0 && dirty.begin()->y == 0,
         "distance key: underfeet missing before far holes");
  Expect((dirty.begin() + 1)->x == 0 && (dirty.begin() + 1)->y == 2,
         "distance key: near remesh before dist-1");
  Expect((dirty.begin() + 2)->x == 1, "distance key: dist-1 before dist-3");
  Expect((dirty.begin() + 3)->x == 3, "distance key: farthest last");

  // Vertical priority within same horiz ring.
  dirty.Clear();
  dirty.MarkDirty(glm::ivec3{0, 0, 0});
  dirty.MarkDirty(glm::ivec3{0, 3, 0});
  dirty.MarkDirty(glm::ivec3{0, 1, 0});
  dirty.SortByDistanceKey(glm::ivec3{0, 0, 0}, /*preferred_cy=*/3,
                          /*prefer_lower_cy=*/false, /*vertical_valid=*/true,
                          {});
  Expect(dirty.begin()->y == 3, "above water: preferred cy should drain first");

  dirty.Clear();
  dirty.MarkDirty(glm::ivec3{0, 3, 0});
  dirty.MarkDirty(glm::ivec3{0, 0, 0});
  dirty.MarkDirty(glm::ivec3{0, 1, 0});
  dirty.SortByDistanceKey(glm::ivec3{0, 0, 0}, /*preferred_cy=*/3,
                          /*prefer_lower_cy=*/true, /*vertical_valid=*/true,
                          {});
  Expect(dirty.begin()->y == 0, "underwater: lower cy should drain first");

  std::cout << "world_mesh_service_test: OK" << std::endl;
  return 0;
}
