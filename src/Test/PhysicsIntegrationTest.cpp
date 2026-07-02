#include "Blocks/BlockDefinitionStorage.h"
#include "World/Core/BlockWorld.h"
#include "World/Physics/LiquidSimulationSystem.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <unordered_map>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "physics_integration_test: " << message << std::endl;
    std::exit(1);
  }
}

int main()
{
  auto definitions = std::make_shared<cutum::UBlockDefinitionStorage>();
  constexpr cutum::BlockId kWater = 9;
  constexpr cutum::BlockId kStone = 8;
  cutum::BlockDefinition stone;
  stone.Name = "stone";
  stone.Physics = cutum::BlockPhysicsProfile::Solid();
  cutum::BlockDefinition water;
  water.Name = "water";
  water.Physics.IsLiquid = true;
  water.Physics.LiquidRenewable = true;
  std::unordered_map<cutum::BlockId, cutum::BlockDefinition> by_id;
  by_id[kStone] = stone;
  by_id[kWater] = water;
  std::unordered_map<std::string, cutum::BlockId> name_to_id;
  name_to_id["stone"] = kStone;
  name_to_id["water"] = kWater;
  definitions->ReplaceAll(std::move(by_id), std::move(name_to_id));

  cutum::UBlockWorld world;
  for (int x = 0; x < 5; ++x)
  {
    for (int z = 0; z < 5; ++z)
    {
      world.SetBlock(glm::ivec3(x, 9, z), kStone);
      world.SetBlock(glm::ivec3(x, 10, z), kWater);
    }
  }
  world.SetBlock(glm::ivec3(0, 10, 2), cutum::BLOCK_AIR);

  cutum::ULiquidSimulationSystem liquid;
  liquid.ShadowMode = false;

  for (uint64_t tick = 0; tick < 20; ++tick)
  {
    for (int x = 0; x < 5; ++x)
    {
      for (int z = 0; z < 5; ++z)
      {
        const glm::ivec3 pos(x, 10, z);
        liquid.TickBlock(world, *definitions, tick, pos);
      }
    }
  }

  Expect(world.GetBlock(glm::ivec3(0, 10, 2)) == kWater,
         "broken wall cell should be filled with water");

  int liquid_count = 0;
  for (int x = 0; x < 5; ++x)
  {
    for (int z = 0; z < 5; ++z)
    {
      const cutum::BlockDefinition *def =
          definitions->GetById(world.GetBlock(glm::ivec3(x, 10, z)));
      if (def != nullptr && def->Physics.IsLiquid)
      {
        ++liquid_count;
      }
    }
  }
  Expect(liquid_count == 25, "renewable source water count should remain 25");

  std::cout << "physics_integration_test: OK" << std::endl;
  return 0;
}
