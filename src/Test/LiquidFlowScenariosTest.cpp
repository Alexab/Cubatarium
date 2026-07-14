#include "Blocks/BlockDefinitionStorage.h"
#include "World/Core/BlockWorld.h"
#include "World/Physics/FluidSpreadSystem.h"
#include "World/Math/FluidCellState.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <vector>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "liquid_flow_scenarios_test: " << message << std::endl;
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
  water.Physics.LiquidViscosity = 1.0f;
  water.Physics.FluidSpreadPeriodTicks = 5;
  water.Physics.FluidMaxLevel = 7;
  cutum::BlockDefinition lava;
  lava.Name = "lava";
  lava.Physics.IsLiquid = true;
  lava.Physics.Floodable = true;
  lava.Physics.LiquidViscosity = 1.0f;
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

static int CountBlock(const cutum::UBlockWorld &world, cutum::BlockId id, int y,
                      int min_x, int max_x, int min_z, int max_z)
{
  int count = 0;
  for (int x = min_x; x <= max_x; ++x)
  {
    for (int z = min_z; z <= max_z; ++z)
    {
      if (world.GetBlock(glm::ivec3(x, y, z)) == id)
      {
        ++count;
      }
    }
  }
  return count;
}

static void RunRegionTicks(cutum::UBlockWorld &world,
                           const cutum::UBlockDefinitionStorage &definitions,
                           cutum::UFluidSpreadSystem &liquid, int y,
                           int min_x, int max_x, int min_z, int max_z,
                           uint64_t ticks)
{
  for (uint64_t tick = 0; tick < ticks; ++tick)
  {
    for (int x = min_x; x <= max_x; ++x)
    {
      for (int z = min_z; z <= max_z; ++z)
      {
        liquid.TickBlock(world, definitions, tick, glm::ivec3(x, y, z));
      }
    }
  }
}

static void TestLavaFallBelow(const cutum::UBlockDefinitionStorage &definitions)
{
  constexpr cutum::BlockId kStone = 8;
  constexpr cutum::BlockId kLava = 11;
  cutum::UBlockWorld world;
  for (int x = 0; x < 3; ++x)
  {
    for (int z = 0; z < 3; ++z)
    {
      world.SetBlock(glm::ivec3(x, 8, z), kStone);
    }
  }
  world.SetBlock(glm::ivec3(0, 10, 1), kStone);
  world.SetBlock(glm::ivec3(2, 10, 1), kStone);
  world.SetBlock(glm::ivec3(1, 10, 0), kStone);
  world.SetBlock(glm::ivec3(1, 10, 2), kStone);
  world.SetBlock(glm::ivec3(1, 9, 1), cutum::BLOCK_AIR);
  world.SetBlock(glm::ivec3(1, 10, 1), kLava);
  world.SetFluidState(glm::ivec3(1, 10, 1), cutum::FluidCellState::Source());

  cutum::UFluidSpreadSystem liquid;
  liquid.ShadowMode = false;
  for (uint64_t tick = 0; tick < 60; ++tick)
  {
    liquid.TickBlock(world, definitions, tick, glm::ivec3(1, 10, 1));
  }

  Expect(world.GetBlock(glm::ivec3(1, 10, 1)) == kLava,
         "lava source should remain when falling below");
  Expect(world.GetBlock(glm::ivec3(1, 9, 1)) == kLava,
         "lava should fall below without breaking side blocks");
}

static void TestWater2x2Pit(const cutum::UBlockDefinitionStorage &definitions)
{
  constexpr cutum::BlockId kStone = 8;
  constexpr cutum::BlockId kWater = 9;
  cutum::UBlockWorld world;
  for (int x = 0; x < 4; ++x)
  {
    for (int z = 0; z < 4; ++z)
    {
      world.SetBlock(glm::ivec3(x, 9, z), kStone);
      world.SetBlock(glm::ivec3(x, 10, z), kStone);
      world.SetBlock(glm::ivec3(x, 11, z), kStone);
    }
  }
  for (int x = 1; x <= 2; ++x)
  {
    for (int z = 1; z <= 2; ++z)
    {
      world.SetBlock(glm::ivec3(x, 10, z), cutum::BLOCK_AIR);
    }
  }
  world.SetBlock(glm::ivec3(1, 10, 1), kWater);
  world.SetFluidState(glm::ivec3(1, 10, 1), cutum::FluidCellState::Source());

  cutum::UFluidSpreadSystem liquid;
  liquid.ShadowMode = false;
  RunRegionTicks(world, definitions, liquid, 10, 0, 3, 0, 3, 120);

  Expect(CountBlock(world, kWater, 10, 1, 2, 1, 2) == 4,
         "2x2 pit should fill within two spread passes");
  Expect(world.GetBlock(glm::ivec3(1, 10, 1)) == kWater,
         "renewable water source cell should remain water");
}

static void TestWater1x1Pit(const cutum::UBlockDefinitionStorage &definitions)
{
  constexpr cutum::BlockId kStone = 8;
  constexpr cutum::BlockId kWater = 9;
  cutum::UBlockWorld world;
  for (int x = 0; x < 3; ++x)
  {
    for (int z = 0; z < 3; ++z)
    {
      world.SetBlock(glm::ivec3(x, 9, z), kStone);
      world.SetBlock(glm::ivec3(x, 10, z), kStone);
    }
  }
  world.SetBlock(glm::ivec3(1, 10, 1), cutum::BLOCK_AIR);
  world.SetBlock(glm::ivec3(1, 10, 1), kWater);
  world.SetFluidState(glm::ivec3(1, 10, 1), cutum::FluidCellState::Source());

  cutum::UFluidSpreadSystem liquid;
  liquid.ShadowMode = false;
  liquid.TickBlock(world, definitions, 0, glm::ivec3(1, 10, 1));

  Expect(world.GetBlock(glm::ivec3(1, 10, 1)) == kWater,
         "water should remain in a 1x1 pit after placement");
}

static void TestWaterHoleInFloor(const cutum::UBlockDefinitionStorage &definitions)
{
  constexpr cutum::BlockId kStone = 8;
  constexpr cutum::BlockId kWater = 9;
  cutum::UBlockWorld world;
  for (int x = 0; x < 5; ++x)
  {
    for (int z = 0; z < 5; ++z)
    {
      world.SetBlock(glm::ivec3(x, 8, z), kStone);
      world.SetBlock(glm::ivec3(x, 10, z), kWater);
    }
  }
  world.SetBlock(glm::ivec3(2, 10, 2), cutum::BLOCK_AIR);
  world.SetBlock(glm::ivec3(2, 9, 2), cutum::BLOCK_AIR);
  world.SetBlock(glm::ivec3(2, 8, 2), cutum::BLOCK_AIR);

  cutum::UFluidSpreadSystem liquid;
  liquid.ShadowMode = false;
  RunRegionTicks(world, definitions, liquid, 10, 0, 4, 0, 4, 40);

  Expect(world.GetBlock(glm::ivec3(2, 10, 2)) == kWater,
         "surface hole above void should be filled by water");
}

static void TestWaterWallGap(const cutum::UBlockDefinitionStorage &definitions)
{
  constexpr cutum::BlockId kWater = 9;
  cutum::UBlockWorld world;
  for (int x = 0; x < 3; ++x)
  {
    for (int z = 0; z < 3; ++z)
    {
      world.SetBlock(glm::ivec3(x, 10, z), kWater);
    }
  }
  world.SetBlock(glm::ivec3(1, 10, 1), cutum::BLOCK_AIR);

  cutum::UFluidSpreadSystem liquid;
  liquid.ShadowMode = false;
  RunRegionTicks(world, definitions, liquid, 10, 0, 2, 0, 2, 20);

  Expect(world.GetBlock(glm::ivec3(1, 10, 1)) == kWater,
         "gap in water wall should be refilled");
}

static void TestWaterRemovalMatrix(const cutum::UBlockDefinitionStorage &definitions)
{
  constexpr cutum::BlockId kStone = 8;
  constexpr cutum::BlockId kWater = 9;
  const std::vector<glm::ivec3> removed_cells = {
      glm::ivec3(0, 10, 0), glm::ivec3(2, 10, 0), glm::ivec3(0, 10, 2),
      glm::ivec3(2, 10, 2), glm::ivec3(1, 10, 0), glm::ivec3(0, 10, 1),
      glm::ivec3(2, 10, 1), glm::ivec3(1, 10, 2)};

  int scenario = 0;
  for (const glm::ivec3 &removed : removed_cells)
  {
    cutum::UBlockWorld world;
    for (int x = 0; x < 3; ++x)
    {
      for (int z = 0; z < 3; ++z)
      {
        world.SetBlock(glm::ivec3(x, 9, z), kStone);
        world.SetBlock(glm::ivec3(x, 10, z), kWater);
      }
    }
    world.SetBlock(removed, cutum::BLOCK_AIR);
    if (removed.y > 9)
    {
      world.SetBlock(glm::ivec3(removed.x, 9, removed.z), cutum::BLOCK_AIR);
    }

    cutum::UFluidSpreadSystem liquid;
    liquid.ShadowMode = false;
    RunRegionTicks(world, definitions, liquid, 10, 0, 2, 0, 2, 30);

    if (world.GetBlock(removed) != kWater)
    {
      std::cerr << "liquid_flow_scenarios_test: matrix scenario " << scenario
                << " failed to fill removed cell at (" << removed.x << ","
                << removed.y << "," << removed.z << ")" << std::endl;
      std::exit(1);
    }
    ++scenario;
  }
}

static void TestLavaIsolatedSpread(const cutum::UBlockDefinitionStorage &definitions)
{
  constexpr cutum::BlockId kStone = 8;
  constexpr cutum::BlockId kLava = 11;
  cutum::UBlockWorld world;
  for (int x = 0; x < 5; ++x)
  {
    for (int z = 0; z < 5; ++z)
    {
      world.SetBlock(glm::ivec3(x, 9, z), kStone);
    }
  }
  world.SetBlock(glm::ivec3(2, 10, 2), kLava);
  world.SetFluidState(glm::ivec3(2, 10, 2), cutum::FluidCellState::Source());

  cutum::UFluidSpreadSystem liquid;
  liquid.ShadowMode = false;
  RunRegionTicks(world, definitions, liquid, 10, 0, 4, 0, 4, 120);

  Expect(world.GetBlock(glm::ivec3(2, 10, 2)) == kLava,
         "lava source should remain at placement");
  Expect(world.GetFluidState(glm::ivec3(2, 10, 2)).IsSource(),
         "lava source fluid state should remain");
}

int main()
{
  const auto definitions = MakeDefinitions();

  TestLavaFallBelow(*definitions);
  TestLavaIsolatedSpread(*definitions);
  TestWater1x1Pit(*definitions);
  TestWater2x2Pit(*definitions);
  TestWaterHoleInFloor(*definitions);
  TestWaterWallGap(*definitions);
  TestWaterRemovalMatrix(*definitions);

  std::cout << "liquid_flow_scenarios_test: OK" << std::endl;
  return 0;
}
