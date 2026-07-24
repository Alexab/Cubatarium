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

  // Missing-mesh class wins over nearer remesh (focus-band holes first).
  dirty.Clear();
  dirty.MarkDirty(glm::ivec3{3, 0, 0}); // far missing
  dirty.MarkDirty(glm::ivec3{0, 2, 0}); // near remesh
  dirty.MarkDirty(glm::ivec3{0, 0, 0}); // near missing
  dirty.MarkDirty(glm::ivec3{1, 0, 0}); // dist 1 missing
  auto missing = [](glm::ivec3 c)
  { return c.y == 0; };
  dirty.SortByDistanceKey(glm::ivec3{0, 0, 0}, /*preferred_cy=*/0,
                          /*prefer_lower_cy=*/false, /*vertical_valid=*/true,
                          missing);
  Expect(dirty.begin()->x == 0 && dirty.begin()->y == 0,
         "missing-first: underfeet missing before other missing");
  Expect((dirty.begin() + 1)->x == 1 && (dirty.begin() + 1)->y == 0,
         "missing-first: dist-1 missing before far missing");
  Expect((dirty.begin() + 2)->x == 3 && (dirty.begin() + 2)->y == 0,
         "missing-first: far missing before near remesh");
  Expect((dirty.begin() + 3)->x == 0 && (dirty.begin() + 3)->y == 2,
         "missing-first: near remesh last among this set");

  // Forward bias: ahead missing drains before side missing at same Chebyshev.
  dirty.Clear();
  dirty.MarkDirty(glm::ivec3{-2, 0, 0}); // ahead (west) when facing -X
  dirty.MarkDirty(glm::ivec3{0, 0, 2});  // side
  dirty.SortByDistanceKey(glm::ivec3{0, 0, 0}, 0, false, true, missing,
                          /*forward_bias_k=*/1.0f, glm::vec2(-1.0f, 0.0f));
  Expect(dirty.begin()->x == -2,
         "forward bias: ahead chunk before side at equal Chebyshev");

  // Vertical priority within same class + same effective dist.
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
