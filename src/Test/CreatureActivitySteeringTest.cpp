#include "Activity/Helpers/CreatureActivitySteering.h"

#include <cassert>
#include <cstdlib>
#include <glm/glm.hpp>
#include <iostream>
#include <vector>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "creature_activity_steering_test: " << message << std::endl;
    std::exit(1);
  }
}

int main()
{
  const glm::vec3 prev(0.0f, 64.0f, 0.0f);
  const glm::vec3 stuck(0.0f, 64.0f, 0.0f);
  const glm::vec3 moved(0.2f, 64.0f, 0.0f);
  Expect(cutum::IsLocomotionStuck(prev, stuck, 0.1f, 0.05f),
         "zero movement should be stuck");
  Expect(!cutum::IsLocomotionStuck(prev, moved, 0.1f, 0.05f),
         "fast movement should not be stuck");

  const glm::vec3 base(1.0f, 0.0f, 0.0f);
  const glm::vec3 sep(0.0f, 0.0f, 1.0f);
  const glm::vec3 blended =
      cutum::BlendLocomotionDirection(base, sep, 0.5f);
  Expect(std::abs(blended.x) > 0.1f && std::abs(blended.z) > 0.1f,
         "blend should mix base and separation");

  std::vector<cutum::CreatureNeighborView> neighbors;
  cutum::CreatureNeighborView near_neighbor;
  near_neighbor.Id = 2;
  near_neighbor.bodyOrigin = glm::vec3(0.5f, 0.0f, 0.0f);
  neighbors.push_back(near_neighbor);
  const glm::vec3 push = cutum::ComputeSeparationDirection(
      glm::vec3(0.0f), glm::vec3(0.6f, 1.8f, 0.6f), neighbors, 1.0f);
  Expect(push.x < -0.1f, "separation should push away from neighbor");

  const float radius = cutum::SeparationQueryRadius(glm::vec3(0.6f, 1.8f, 0.6f));
  Expect(radius > 1.0f, "separation radius should exceed body footprint");

  std::cout << "creature_activity_steering_test: OK" << std::endl;
  return 0;
}
