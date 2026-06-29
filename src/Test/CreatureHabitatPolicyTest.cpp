#include "Creatures/Movement/CreatureHabitatPolicy.h"

#include <cassert>
#include <cstdlib>
#include <iostream>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "creature_habitat_policy_test: " << message << std::endl;
    std::exit(1);
  }
}

int main()
{
  using namespace cutum;

  EnvironmentSample dryGround;
  dryGround.onSolidGround = true;
  dryGround.inFluid = false;
  dryGround.inWater = false;
  dryGround.inLava = false;

  EnvironmentSample water;
  water.inWater = true;
  water.inFluid = true;
  water.onSolidGround = false;

  EnvironmentSample air;
  air.inOpenAir = true;
  air.inFluid = false;
  air.onSolidGround = false;
  air.bodyBlocked = false;

  Expect(HabitatAllows(CreatureHabitat::Terrestrial, HabitatContext::WanderCurrent,
                       dryGround),
         "terrestrial wander current on ground");
  Expect(!HabitatAllows(CreatureHabitat::Terrestrial, HabitatContext::WanderCurrent,
                        water),
         "terrestrial wander current in water");
  Expect(HabitatAllows(CreatureHabitat::Terrestrial, HabitatContext::WanderCurrent,
                       air),
         "terrestrial wander current in air");
  Expect(HabitatAllows(CreatureHabitat::Terrestrial, HabitatContext::MoveApply,
                       dryGround),
         "terrestrial move apply on ground");
  Expect(!HabitatAllows(CreatureHabitat::Terrestrial, HabitatContext::MoveApply,
                        air),
         "terrestrial move apply in open air");
  EnvironmentSample blockedOnGround;
  blockedOnGround.onSolidGround = false;
  blockedOnGround.bodyBlocked = true;
  blockedOnGround.inFluid = false;
  Expect(HabitatAllows(CreatureHabitat::Terrestrial, HabitatContext::MoveApply,
                       blockedOnGround),
         "terrestrial move apply with body block contact");
  Expect(HabitatAllows(CreatureHabitat::Aquatic, HabitatContext::WanderCurrent,
                       water),
         "aquatic wander in water");
  Expect(!HabitatAllows(CreatureHabitat::Aquatic, HabitatContext::WanderCurrent,
                        dryGround),
         "aquatic wander on dry land");

  std::cout << "creature_habitat_policy_test: OK" << std::endl;
  return 0;
}
