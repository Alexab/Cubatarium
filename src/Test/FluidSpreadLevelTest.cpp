#include "Blocks/BlockDefinitionStorage.h"
#include "World/Core/BlockWorld.h"
#include "World/Physics/FluidSpreadSystem.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <unordered_map>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "fluid_spread_level_test: " << message << std::endl;
    std::exit(1);
  }
}

static std::shared_ptr<cutum::UBlockDefinitionStorage> MakeDefinitions()
{
  auto definitions = std::make_shared<cutum::UBlockDefinitionStorage>();
  constexpr cutum::BlockId kStone = 8;
  constexpr cutum::BlockId kWater = 9;
  constexpr cutum::BlockId kLava = 11;
  cutum::BlockDefinition stone;
  stone.Name = "stone";
  stone.Physics = cutum::BlockPhysicsProfile::Solid();
  cutum::BlockDefinition water;
  water.Name = "water";
  water.Physics.IsLiquid = true;
  water.Physics.Floodable = true;
  water.Physics.FluidSpreadPeriodTicks = 5;
  water.Physics.FluidMaxLevel = 7;
  cutum::BlockDefinition lava;
  lava.Name = "lava";
  lava.Physics.IsLiquid = true;
  lava.Physics.Floodable = true;
  lava.Physics.FluidSpreadPeriodTicks = 30;
  lava.Physics.FluidMaxLevel = 3;
  std::unordered_map<cutum::BlockId, cutum::BlockDefinition> by_id;
  by_id[kStone] = stone;
  by_id[kWater] = water;
  by_id[kLava] = lava;
  std::unordered_map<std::string, cutum::BlockId> name_to_id;
  name_to_id["stone"] = kStone;
  name_to_id["water"] = kWater;
  name_to_id["lava"] = kLava;
  definitions->ReplaceAll(std::move(by_id), std::move(name_to_id));
  return definitions;
}

static void RunTicks(cutum::UBlockWorld &world,
                     const cutum::UBlockDefinitionStorage &definitions,
                     cutum::UFluidSpreadSystem &fluid, uint64_t ticks)
{
  for (uint64_t tick = 0; tick < ticks; ++tick)
  {
    world.ForEachBlock([&](glm::ivec3 pos, cutum::BlockId id)
                       {
                         (void)id;
                         fluid.TickBlock(world, definitions, tick, pos);
                       });
  }
}

int main()
{
  // Stub until F2: compile and link; full scenarios enabled after spread system.
  const auto definitions = MakeDefinitions();
  cutum::UBlockWorld world;
  cutum::UFluidSpreadSystem fluid;
  fluid.ShadowMode = false;

  constexpr cutum::BlockId kStone = 8;
  constexpr cutum::BlockId kWater = 9;
  for (int x = 0; x <= 3; ++x)
  {
    for (int z = 0; z <= 3; ++z)
    {
      world.SetBlock(glm::ivec3(x, 9, z), kStone);
    }
  }
  world.SetBlock(glm::ivec3(1, 10, 1), kWater);
  world.SetFluidState(glm::ivec3(1, 10, 1), cutum::FluidCellState::Source());
  RunTicks(world, *definitions, fluid, 200);

  const cutum::FluidCellState source =
      world.GetFluidState(glm::ivec3(1, 10, 1));
  Expect(source.IsSource(), "water source stable after spread ticks");

  std::cout << "fluid_spread_level_test: OK" << std::endl;
  return 0;
}
