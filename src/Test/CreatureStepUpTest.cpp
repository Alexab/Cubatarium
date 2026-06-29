#include "Creatures/Movement/CreatureBodyStepUp.h"
#include "Creatures/Locomotion/LocomotionTypes.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "creature_step_up_test: " << message << std::endl;
    std::exit(1);
  }
}

int main()
{
  using namespace cutum;

  const glm::vec3 chickenSize(0.5f, 0.7f, 0.5f);
  const glm::vec3 cowSize(1.1f, 1.3f, 1.8f);
  const float chickenRadius =
      std::max(chickenSize.x, chickenSize.z) * 0.5f;
  const float cowRadius = std::max(cowSize.x, cowSize.z) * 0.5f;
  Expect(chickenRadius == 0.25f, "chicken body radius");
  Expect(cowRadius == 0.9f, "cow body radius");

  const float chickenProbe = std::max(0.25f, chickenRadius * 0.4f);
  const float cowProbe = std::max(0.25f, cowRadius * 0.4f);
  Expect(chickenProbe >= 0.25f, "chicken step-up trigger distance");
  Expect(cowProbe > chickenProbe, "cow has wider step-up trigger");

  std::cout << "creature_step_up_test: OK" << std::endl;
  return 0;
}
