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

  // Partial sort: front window matches full sort order; tail may be unsorted.
  dirty.Clear();
  for (int x = 5; x >= 0; --x)
  {
    dirty.MarkDirty(glm::ivec3{x, 0, 0});
  }
  dirty.PartialSortByDistanceKey(glm::ivec3{0, 0, 0}, 0, false, true, missing,
                                 /*keep_front=*/3);
  Expect(dirty.begin()->x == 0, "partial_sort: nearest missing first");
  Expect((dirty.begin() + 1)->x == 1, "partial_sort: second nearest");
  Expect((dirty.begin() + 2)->x == 2, "partial_sort: third nearest");

  dirty.Clear();
  const glm::ivec3 ray(-5, -3, 3);
  Expect(!dirty.PrioritizeScreenRayFirstMesh(ray) &&
             !dirty.IsScreenRayFirstMesh(ray),
         "ray urgency cannot create a detached FirstMesh owner");
  dirty.MarkDirtyPriority(ray);
  dirty.AdvanceScheduleFrame();
  const auto ray_age = dirty.GetEnqueueAgeFrames(ray);
  Expect(dirty.PrioritizeScreenRayFirstMesh(ray) &&
             dirty.PrioritizeScreenRayFirstMesh(ray) && dirty.GetCount() == 1 &&
             dirty.GetEnqueueAgeFrames(ray) == ray_age,
         "repeated ray witnesses preserve one dirty owner and its age");
  dirty.MarkDirtyPriority(ray);
  dirty.SortByDistanceKey({-3, -3, 3}, -3, false, true, {});
  dirty.PrioritizeAgedNearHorizontal({-3, -3, 3}, 4, 0);
  Expect(dirty.IsScreenRayFirstMesh(ray),
         "queue sorting and re-admission retain exact slice urgency");
  dirty.RemoveAt(dirty.begin());
  Expect(!dirty.IsScreenRayFirstMesh(ray) && dirty.empty(),
         "consuming a FirstMesh queue entry retires its urgency hint");
  dirty.MarkDirtyPriority(ray);
  Expect(!dirty.IsScreenRayFirstMesh(ray),
         "a later FirstMesh incarnation cannot inherit a consumed hint");
  dirty.PrioritizeScreenRayFirstMesh(ray);
  dirty.Erase(ray);
  Expect(!dirty.IsScreenRayFirstMesh(ray),
         "eviction retires queued ray urgency");
  dirty.MarkDirtyPriority(ray);
  dirty.PrioritizeScreenRayFirstMesh(ray);
  dirty.Clear();
  Expect(!dirty.IsScreenRayFirstMesh(ray),
         "world cancellation retires ray urgency");
  dirty.MarkDirty(ray);
  Expect(!dirty.PrioritizeScreenRayFirstMesh(ray),
         "FirstMesh urgency cannot reclassify existing remesh work");

  std::cout << "world_mesh_service_test: OK" << std::endl;
  return 0;
}
