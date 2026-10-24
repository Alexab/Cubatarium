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
    // Characterizes CREATURES_AUDIT_2026-10_RU.md finding M1: insert puts an
    // entry only into its origin cell while queries gather all cells of the
    // query volume, so a wide body whose origin cell is outside the query's
    // gathered cells is missed. The expectations marked M1 flip to the
    // opposite value in refactor/4-creatures-bugfixes.
    UCreatureSpatialIndex index;
    const glm::vec3 wideOrigin(0.5f, 64.f, 0.5f);
    const glm::vec3 wideHalf(8.f, 0.9f, 0.3f);
    index.Upsert(10, wideOrigin, wideHalf);

    // Cells are 2x2 blocks; the wide body spans x blocks [-8, 9] but lives
    // only in cell (0, 0). The query sits inside the body's AABB near its far
    // edge; its gathered cells cover x cells [2, 4] and exclude (0, 0).
    CollisionVolume insideFarEdge{glm::vec3(7.5f, 64.f, 0.5f),
                                  glm::vec3(0.4f, 0.4f, 0.4f)};
    Expect(AabbOverlap(insideFarEdge.center, insideFarEdge.halfExtents,
                       wideOrigin, wideHalf),
           "M1 setup: the query volume really overlaps the wide body", failures);
    // M1: known miss — returns false today, true after the fix.
    Expect(!index.AnyCreatureVolumeOverlaps(insideFarEdge, 0),
           "M1: wide body is missed when its origin cell is outside the "
           "query cells (bug, flips in refactor/4)", failures);

    // Same geometry queried near the origin cell is found today.
    CollisionVolume nearOrigin{glm::vec3(1.5f, 64.f, 0.5f),
                               glm::vec3(0.4f, 0.4f, 0.4f)};
    Expect(index.AnyCreatureVolumeOverlaps(nearOrigin, 0),
           "M1 contrast: query covering the origin cell finds the body", failures);
  }

  if (failures != 0)
  {
    return 1;
  }
  std::cout << "creature_spatial_index_test: OK\n";
  return 0;
}
