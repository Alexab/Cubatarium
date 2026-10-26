#include "World/Environment/CreatureSpatialIndex.h"

#include <algorithm>
#include <iostream>
#include <vector>

namespace
{

void Expect(bool condition, const char *message, int &failures)
{
  if (!condition)
  {
    std::cerr << "creature_spatial_index_test: " << message << '\n';
    ++failures;
  }
}

bool Contains(const std::vector<cutum::CreatureId> &ids, cutum::CreatureId id)
{
  return std::find(ids.begin(), ids.end(), id) != ids.end();
}

} // namespace

int main()
{
  using namespace cutum;
  int failures = 0;
  const glm::vec3 half(0.3f, 0.9f, 0.3f);

  {
    UCreatureSpatialIndex index;
    const glm::vec3 a(0.5f, 64.f, 0.5f);
    index.Upsert(1, a, half);

    auto ids = index.QueryRadius(a, 4.f, 0);
    Expect(ids.size() == 1 && ids[0] == 1,
           "radius query finds an upserted creature", failures);
    Expect(index.QueryRadius(a, 4.f, 1).empty(),
           "radius query can skip the caller by id", failures);

    auto neighbors = index.QueryNeighbors(a, 4.f, 0);
    Expect(neighbors.size() == 1 && neighbors[0].Id == 1 &&
               neighbors[0].bodyOrigin == a,
           "neighbor query returns id and origin", failures);

    CollisionVolume atA{a, half};
    Expect(index.AnyCreatureVolumeOverlaps(atA, 0),
           "overlap query detects a creature in the same cell", failures);
    Expect(!index.AnyCreatureVolumeOverlaps(atA, 1),
           "overlap query can skip the caller by id", failures);
    Expect(!index.AnyCreatureVolumeOverlaps(CollisionVolume{a + glm::vec3(20.f, 0.f, 0.f), half}, 0),
           "overlap query ignores distant volumes", failures);
  }

  {
    UCreatureSpatialIndex index;
    const glm::vec3 a(0.5f, 64.f, 0.5f);
    index.Upsert(1, a, half);
    index.Upsert(2, glm::vec3(20.5f, 64.f, 0.5f), half);

    CollisionVolume atB{glm::vec3(20.5f, 64.f, 0.5f), half};
    Expect(index.AnyCreatureVolumeOverlaps(atB, 0),
           "second creature in another cell is found", failures);
    Expect(!index.AnyCreatureVolumeOverlaps(atB, 2),
           "overlap skips the second creature by id", failures);

    index.Upsert(1, glm::vec3(40.5f, 64.f, 0.5f), half);
    Expect(!index.AnyCreatureVolumeOverlaps(CollisionVolume{a, half}, 0),
           "moving a creature removes the old cell entry", failures);
    Expect(index.AnyCreatureVolumeOverlaps(
               CollisionVolume{glm::vec3(40.5f, 64.f, 0.5f), half}, 2),
           "moved creature is found at its new position", failures);

    index.Remove(1);
    Expect(!index.AnyCreatureVolumeOverlaps(
               CollisionVolume{glm::vec3(40.5f, 64.f, 0.5f), half}, 0),
           "remove drops the creature from all queries", failures);
    Expect(Contains(index.QueryRadius(glm::vec3(20.5f, 64.f, 0.5f), 4.f, 0), 2),
           "remove leaves other creatures intact", failures);

    index.PruneExcept({2});
    Expect(!index.QueryRadius(glm::vec3(20.5f, 64.f, 0.5f), 4.f, 0).empty(),
           "prune keeps alive creatures", failures);
    index.PruneExcept({});
    Expect(index.QueryRadius(glm::vec3(20.5f, 64.f, 0.5f), 4.f, 0).empty(),
           "prune with an empty alive set clears the index", failures);
  }

  {
    // Regression for CREATURES_AUDIT_2026-10_RU.md finding M1: insert used to
    // put an entry only into its origin cell while queries gather all cells
    // of the query volume, so a wide body whose origin cell was outside the
    // query's gathered cells was missed. Insert now covers every cell of the
    // body's AABB, mirroring removal.
    UCreatureSpatialIndex index;
    const glm::vec3 wideOrigin(0.5f, 64.f, 0.5f);
    const glm::vec3 wideHalf(8.f, 0.9f, 0.3f);
    index.Upsert(10, wideOrigin, wideHalf);

    // Cells are 2x2 blocks; the wide body spans x blocks [-8, 9]. The query
    // sits inside the body's AABB near its far edge, its gathered cells
    // covering x cells [2, 4] — nowhere near the origin cell (0, 0).
    CollisionVolume insideFarEdge{glm::vec3(7.5f, 64.f, 0.5f),
                                  glm::vec3(0.4f, 0.4f, 0.4f)};
    Expect(AabbOverlap(insideFarEdge.center, insideFarEdge.halfExtents,
                       wideOrigin, wideHalf),
           "M1 setup: the query volume really overlaps the wide body", failures);
    Expect(index.AnyCreatureVolumeOverlaps(insideFarEdge, 0),
           "M1 regression: wide body is found even when its origin cell is "
           "outside the query cells", failures);

    CollisionVolume nearOrigin{glm::vec3(1.5f, 64.f, 0.5f),
                               glm::vec3(0.4f, 0.4f, 0.4f)};
    Expect(index.AnyCreatureVolumeOverlaps(nearOrigin, 0),
           "M1 contrast: query covering the origin cell finds the body", failures);

    // Multi-cell insert must not duplicate the entry in the origin cell and
    // removal must still clear every cell (no ghosts after remove).
    index.Remove(10);
    Expect(!index.AnyCreatureVolumeOverlaps(insideFarEdge, 0) &&
               !index.AnyCreatureVolumeOverlaps(nearOrigin, 0),
           "M1: remove clears the wide body from all cells", failures);
  }

  {
    // Entries occupy several cells, so radius/neighbor queries walking those
    // cells must still report each creature exactly once.
    UCreatureSpatialIndex index;
    index.Upsert(20, glm::vec3(0.5f, 64.f, 0.5f), glm::vec3(3.f, 0.9f, 3.f));
    const glm::vec3 center(0.5f, 64.f, 0.5f);
    const auto ids = index.QueryRadius(center, 6.f, 0);
    Expect(ids.size() == 1 && ids[0] == 20,
           "radius query reports a multi-cell creature once", failures);
    const auto neighbors = index.QueryNeighbors(center, 6.f, 0);
    Expect(neighbors.size() == 1 && neighbors[0].Id == 20,
           "neighbor query reports a multi-cell creature once", failures);
  }

  {
    // Runtime issue 3 regression: depenetration picks neighbors by volume
    // overlap, not origin distance - a big body whose origin is far outside
    // a small query volume must still be returned.
    UCreatureSpatialIndex index;
    index.Upsert(30, glm::vec3(20.5f, 64.f, 0.5f), glm::vec3(2.5f, 0.6f, 4.f));
    // Query volume overlaps the body edge (body x spans [18,23]) while its
    // center stays 2 blocks from the body origin - origin-radius queries
    // with r<2 would miss it.
    const CollisionVolume atEdge{glm::vec3(18.4f, 64.f, 0.5f),
                                 glm::vec3(0.3f, 0.9f, 0.3f)};
    const auto overlapping =
        index.QueryNeighborsOverlappingVolume(atEdge, 0);
    Expect(overlapping.size() == 1 && overlapping[0].Id == 30,
           "volume query returns a big body overlapping the query edge",
           failures);
    const CollisionVolume far{glm::vec3(10.5f, 64.f, 0.5f),
                              glm::vec3(0.3f, 0.9f, 0.3f)};
    Expect(index.QueryNeighborsOverlappingVolume(far, 0).empty(),
           "volume query ignores non-overlapping bodies", failures);
  }

  if (failures != 0)
  {
    return 1;
  }
  std::cout << "creature_spatial_index_test: OK\n";
  return 0;
}
