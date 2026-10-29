#include "Creatures/Core/CreatureReach.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "creature_reach_test: " << message << std::endl;
    std::exit(1);
  }
}

static bool Near(float a, float b, float eps = 1e-4f)
{
  return std::abs(a - b) < eps;
}

int main()
{
  using namespace cutum;

  const glm::vec3 human(0.6f, 1.8f, 0.6f);
  const glm::vec3 ogre(2.58f, 3.14f, 2.86f);

  // Footprint gap: touching / overlapping bodies have no gap.
  Expect(Near(HorizontalBodyGapXZ(glm::vec3(0.f), human, glm::vec3(0.6f, 0.f, 0.f),
                                  human),
              0.0f),
         "touching bodies have zero gap");
  Expect(Near(HorizontalBodyGapXZ(glm::vec3(0.f), human, glm::vec3(2.0f, 0.f, 0.f),
                                  human),
              1.4f),
         "gap along X is center distance minus half widths");
  Expect(Near(HorizontalBodyGapXZ(glm::vec3(0.f), human,
                                  glm::vec3(1.6f, 0.f, 1.6f), human),
              std::sqrt(2.0f)),
         "diagonal gap combines both axes");

  // Melee distance keeps the old center distance for standard bodies.
  Expect(Near(MeleeDistanceXZ(glm::vec3(0.f), human, glm::vec3(1.2f, 0.f, 0.f),
                              human),
              1.2f),
         "standard pair: melee distance equals center distance");

  // An ogre pressed against the player (closest approach along X) used to be
  // 1.59 center-to-center, beyond its 1.4 attack range: it could never hit.
  const glm::vec3 ogreOrigin(0.f);
  const glm::vec3 playerTouching(0.5f * (ogre.x + human.x), 0.f, 0.f);
  Expect(MeleeDistanceXZ(ogreOrigin, ogre, playerTouching, human) <= 1.4f,
         "ogre touching the player is within its 1.4 attack range");
  Expect(MeleeDistanceXZ(ogreOrigin, ogre,
                         playerTouching + glm::vec3(3.0f, 0.f, 0.f), human) >
             1.4f,
         "ogre three blocks away is out of range");
  // Never longer than the center distance (narrow bodies keep their reach).
  const glm::vec3 thin(0.38f, 1.7f, 0.38f);
  Expect(MeleeDistanceXZ(glm::vec3(0.f), thin, glm::vec3(1.0f, 0.f, 0.f), thin) <=
             1.0f + 1e-4f,
         "narrow pair never exceeds center distance");

  // Ray picking: entry distance, start inside reports zero, miss and range.
  float t = -1.0f;
  Expect(RayIntersectsAabb(glm::vec3(0.f), glm::vec3(1.f, 0.f, 0.f),
                           glm::vec3(2.f, -1.f, -1.f), glm::vec3(3.f, 1.f, 1.f),
                           8.0f, t) &&
             Near(t, 2.0f),
         "ray enters box at x = 2");
  Expect(RayIntersectsAabb(glm::vec3(0.f), glm::vec3(1.f, 0.f, 0.f),
                           glm::vec3(-1.f), glm::vec3(1.f), 8.0f, t) &&
             Near(t, 0.0f),
         "ray starting inside reports t = 0 (why the viewer must be skipped)");
  Expect(!RayIntersectsAabb(glm::vec3(0.f), glm::vec3(1.f, 0.f, 0.f),
                            glm::vec3(2.f, 2.f, -1.f), glm::vec3(3.f, 3.f, 1.f),
                            8.0f, t),
         "ray misses a box beside it");
  Expect(!RayIntersectsAabb(glm::vec3(0.f), glm::vec3(1.f, 0.f, 0.f),
                            glm::vec3(2.f, -1.f, -1.f), glm::vec3(3.f, 1.f, 1.f),
                            1.5f, t),
         "box beyond the reach (or behind the first solid block) is not hit");

  std::cout << "creature_reach_test: ok" << std::endl;
  return 0;
}
