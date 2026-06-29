#include "Creatures/Movement/CreatureSeparationMath.h"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <optional>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "creature_separation_test: " << message << std::endl;
    std::exit(1);
  }
}

int main()
{
  using namespace cutum;

  const std::optional<glm::vec2> separated =
      ComputeOverlapSeparationPushXZ(0.0f, 0.0f, 0.5f, 0.5f, 3.0f, 0.0f, 0.5f,
                                     0.5f);
  Expect(!separated.has_value(), "non-overlapping footprints produce no push");

  const std::optional<glm::vec2> overlap =
      ComputeOverlapSeparationPushXZ(0.0f, 0.0f, 0.5f, 0.5f, 0.25f, 0.0f, 0.5f,
                                    0.5f);
  Expect(overlap.has_value(), "overlapping footprints produce a push");
  Expect(std::abs(overlap->x) > 0.2f, "overlap push moves along X");
  Expect(std::abs(overlap->y) < 1e-4f, "minor-axis overlap uses X push");

  const std::optional<glm::vec2> zOverlap =
      ComputeOverlapSeparationPushXZ(0.0f, 0.0f, 0.5f, 0.5f, 0.0f, 0.25f, 0.5f,
                                     0.5f);
  Expect(zOverlap.has_value(), "Z overlap produces a push");
  Expect(std::abs(zOverlap->y) > 0.2f, "overlap push moves along Z");
  Expect(std::abs(zOverlap->x) < 1e-4f, "minor-axis overlap uses Z push");

  std::cout << "creature_separation_test: OK" << std::endl;
  return 0;
}
