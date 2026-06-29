#include "Creatures/Movement/CreatureBodyProbe.h"
#include "Creatures/Movement/CreatureHabitatPolicy.h"

#include <cassert>
#include <cstdlib>
#include <iostream>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "creature_body_probe_test: " << message << std::endl;
    std::exit(1);
  }
}

int main()
{
  using namespace cutum;

  const glm::vec3 size(0.6f, 1.0f, 0.6f);
  EnvironmentSample ground;
  ground.onSolidGround = true;
  ground.inFluid = false;

  const glm::vec3 origin(1.0f, 10.0f, 2.0f);
  const glm::vec3 resolved(1.4f, 10.0f, 2.0f);
  const BodyMoveResult ok = EvaluateResolvedMove(
      origin, resolved, CreatureHabitat::Terrestrial,
      HabitatContext::WanderTarget, ground, size);
  Expect(!ok.blockedGeometry && ok.habitatOk, "wander probe step on ground");

  const glm::vec3 tiny(1.01f, 10.0f, 2.0f);
  const BodyMoveResult blocked = EvaluateResolvedMove(
      origin, tiny, CreatureHabitat::Terrestrial, HabitatContext::WanderTarget,
      ground, size);
  Expect(blocked.blockedGeometry, "tiny wander step blocked");

  const glm::vec3 smallSize(0.5f, 0.7f, 0.5f);
  Expect(MinWanderProbeXZ(smallSize) < MinWanderProbeXZ(size),
         "small mobs use lower wander probe threshold");

  const BodyMoveResult move = EvaluateResolvedMove(
      origin, resolved, CreatureHabitat::Terrestrial,
      HabitatContext::MoveApply, ground, size);
  Expect(!move.blockedGeometry && move.habitatOk, "move apply on ground");

  std::cout << "creature_body_probe_test: OK" << std::endl;
  return 0;
}
