#include "Creatures/Movement/CreaturePlacement.h"

#include <cassert>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <unordered_set>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "creature_placement_test: " << message << std::endl;
    std::exit(1);
  }
}

int main()
{
  using namespace cutum;

  const std::vector<glm::ivec2> ring = BuildSpawnSearchRing(2);
  Expect(ring.size() == 25u, "ring radius 2 has 25 cells");
  Expect(ring.front().x == 0 && ring.front().y == 0, "ring starts at origin");

  const std::vector<glm::ivec2> ring0 = BuildSpawnSearchRing(0);
  Expect(ring0.size() == 1u, "ring radius 0 is center only");

  std::unordered_set<int> seen;
  for (const glm::ivec2 &cell : ring)
  {
    const int key = cell.x * 100 + cell.y;
    Expect(seen.insert(key).second, "ring cells are unique");
    Expect(std::abs(cell.x) <= 2 && std::abs(cell.y) <= 2,
          "ring cells stay inside radius");
  }

  Expect(std::strcmp(SpawnFailureReasonLabel(SpawnFailureReason::Blocks),
                     "blocks") == 0,
        "spawn failure labels are stable");

  std::cout << "creature_placement_test: OK" << std::endl;
  return 0;
}
