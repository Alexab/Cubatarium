#include "Blocks/BlockDefinitionStorage.h"
#include "World/Core/BlockWorld.h"
#include "World/Physics/FluidSpreadSystem.h"
#include "World/Math/FluidCellState.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <unordered_map>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "liquid_non_renewable_flow_test: " << message << std::endl;
    std::exit(1);
  }
}

int main()
{
  auto definitions = std::make_shared<cutum::UBlockDefinitionStorage>();
  constexpr cutum::BlockId kLava = 11;
  constexpr cutum::BlockId kStone = 8;
  cutum::BlockDefinition stone;
  stone.Name = "stone";
  stone.Physics = cutum::BlockPhysicsProfile::Solid();
  cutum::BlockDefinition lava;
  lava.Name = "lava";
  lava.Physics.IsLiquid = true;
  lava.Physics.Floodable = true;
  lava.Physics.LiquidViscosity = 1.0f;
  lava.Physics.FluidSpreadPeriodTicks = 30;
  lava.Physics.FluidMaxLevel = 3;
  std::unordered_map<cutum::BlockId, cutum::BlockDefinition> by_id;
  by_id[kStone] = stone;
  by_id[kLava] = lava;
  std::unordered_map<std::string, cutum::BlockId> name_to_id;
  name_to_id["stone"] = kStone;
  name_to_id["lava"] = kLava;
  definitions->ReplaceAll(std::move(by_id), std::move(name_to_id));

  cutum::UBlockWorld world;
  for (int x = 0; x < 4; ++x)
  {
    for (int z = 0; z < 4; ++z)
    {
      world.SetBlock(glm::ivec3(x, 9, z), kStone);
      world.SetBlock(glm::ivec3(x, 10, z), kStone);
    }
  }
  for (int x = 1; x <= 2; ++x)
  {
    for (int z = 1; z <= 2; ++z)
    {
      world.SetBlock(glm::ivec3(x, 10, z), cutum::BLOCK_AIR);
    }
  }
  world.SetBlock(glm::ivec3(1, 10, 1), kLava);
  world.SetFluidState(glm::ivec3(1, 10, 1), cutum::FluidCellState::Source());

  cutum::UFluidSpreadSystem liquid;
  liquid.ShadowMode = false;

  glm::ivec3 last_pos(-1, -1, -1);
  int oscillations = 0;
  for (uint64_t tick = 0; tick < 40; ++tick)
  {
    glm::ivec3 current_pos(-1, -1, -1);
    for (int x = 1; x <= 2; ++x)
    {
      for (int z = 1; z <= 2; ++z)
      {
        const glm::ivec3 pos(x, 10, z);
        if (world.GetBlock(pos) == kLava)
        {
          current_pos = pos;
        }
        liquid.TickBlock(world, *definitions, tick, pos);
      }
    }
    if (current_pos.x >= 0 && last_pos.x >= 0 && current_pos == last_pos)
    {
      break;
    }
    if (current_pos.x >= 0 && last_pos.x >= 0 && current_pos != last_pos)
    {
      ++oscillations;
    }
    if (current_pos.x >= 0)
    {
      last_pos = current_pos;
    }
  }

  Expect(oscillations == 0, "lava source should not oscillate in a 2x2 pit");
  Expect(world.GetBlock(glm::ivec3(1, 10, 1)) == kLava,
         "lava source cell should remain stable");
  const cutum::FluidCellState source =
      world.GetFluidState(glm::ivec3(1, 10, 1));
  Expect(source.IsSource(), "placed lava should keep source fluid state");

  std::cout << "liquid_non_renewable_flow_test: OK" << std::endl;
  return 0;
}
