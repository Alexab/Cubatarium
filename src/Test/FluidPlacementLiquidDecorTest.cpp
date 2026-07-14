#include "Test/FluidTestHelpers.h"

#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"
#include "World/Math/FluidCellState.h"
#include "World/Physics/FluidSpreadSystem.h"
#include "World/Physics/FluidUpdateSet.h"

#include <iostream>
#include <memory>

namespace
{

constexpr const char *kTestName = "fluid_placement_liquid_decor_test";
constexpr cutum::BlockId kStone = 8;
constexpr cutum::BlockId kWater = 9;
constexpr cutum::BlockId kTallGrass = 10;
constexpr cutum::BlockId kLava = 11;

static void TestLavaOnAirSpreads(
    const std::shared_ptr<cutum::UBlockDefinitionStorage> &definitions,
    cutum::UFluidSpreadSystem &fluid)
{
  cutum::UBlockWorld world;
  world.SetFluidDefinitions(definitions.get());
  for (int x = 3; x <= 7; ++x)
  {
    for (int z = 3; z <= 7; ++z)
    {
      world.SetBlock(glm::ivec3(x, 9, z), kStone);
    }
  }
  const glm::ivec3 source(5, 10, 5);
  cutum::UFluidSpreadSystem::ApplyFluidFill(
      world, *definitions, source, kLava,
      cutum::FluidCellState::Source().WithKind(cutum::FluidKind::Lava));

  cutum::UFluidUpdateSet queue;
  queue.Enqueue(source);
  FluidTest::RunQueueTicks(world, *definitions, queue, fluid, 2000);

  FluidTest::Expect(world.GetBlock(source) == kLava, kTestName,
                    "lava source block on air");
  FluidTest::Expect(world.GetBlock(glm::ivec3(6, 10, 5)) == kLava, kTestName,
                    "lava source spreads to horizontal air on land");
}

static void TestLavaOnGrassWaterlogs(
    const std::shared_ptr<cutum::UBlockDefinitionStorage> &definitions,
    cutum::UBlockRegistry &registry)
{
  cutum::UBlockWorld world;
  world.SetFluidDefinitions(definitions.get());
  const glm::ivec3 grass_pos(0, 11, 0);
  world.SetBlock(glm::ivec3(0, 10, 0), kStone);
  world.SetBlock(grass_pos, kTallGrass);
  cutum::UFluidSpreadSystem::ApplyFluidFill(
      world, *definitions, grass_pos, kLava,
      cutum::FluidCellState::Source().WithKind(cutum::FluidKind::Lava));

  FluidTest::Expect(world.GetBlock(grass_pos) == kTallGrass, kTestName,
                    "lava on grass preserves decor block");
  FluidTest::Expect(world.GetFluidState(grass_pos).GetKind() ==
                        cutum::FluidKind::Lava,
                    kTestName, "lava on grass sets explicit lava kind");

  const glm::ivec3 chunk_coord = cutum::UChunkManager::WorldToChunk(grass_pos);
  const std::vector<cutum::GreedyQuad> quads =
      FluidTest::BuildFluidMesh(world, registry, grass_pos);
  FluidTest::Expect(
      FluidTest::CountTopFacesAt(quads, kLava, grass_pos, chunk_coord) >= 1,
      kTestName, "lava-waterlogged grass renders lava mesh face");
  FluidTest::Expect(
      FluidTest::CountTopFacesAt(quads, kWater, grass_pos, chunk_coord) == 0,
      kTestName, "lava-waterlogged grass does not render water mesh face");
}

static void TestWaterOnGrassSpreads(
    const std::shared_ptr<cutum::UBlockDefinitionStorage> &definitions,
    cutum::UFluidSpreadSystem &fluid)
{
  cutum::UBlockWorld world;
  world.SetFluidDefinitions(definitions.get());
  const glm::ivec3 grass_pos(2, 11, 2);
  world.SetBlock(glm::ivec3(2, 10, 2), kStone);
  world.SetBlock(grass_pos, kTallGrass);
  world.SetBlock(glm::ivec3(1, 11, 2), kWater);
  world.SetFluidState(glm::ivec3(1, 11, 2),
                      cutum::FluidCellState::Source().WithKind(
                          cutum::FluidKind::Water));
  world.SetBlock(glm::ivec3(3, 11, 2), cutum::BLOCK_AIR);
  cutum::UFluidSpreadSystem::ApplyFluidFill(
      world, *definitions, grass_pos, kWater,
      cutum::FluidCellState::Source().WithKind(cutum::FluidKind::Water));

  FluidTest::Expect(world.GetBlock(grass_pos) == kTallGrass, kTestName,
                    "water on grass preserves decor");
  FluidTest::Expect(world.GetFluidState(grass_pos).GetKind() ==
                        cutum::FluidKind::Water,
                    kTestName, "water on grass sets explicit water kind");

  cutum::UFluidUpdateSet queue;
  queue.Enqueue(grass_pos);
  queue.Enqueue(glm::ivec3(1, 11, 2));
  queue.Enqueue(glm::ivec3(3, 11, 2));
  FluidTest::RunQueueTicks(world, *definitions, queue, fluid, 500);

  FluidTest::Expect(world.GetBlock(glm::ivec3(3, 11, 2)) == kWater, kTestName,
                    "waterlogged grass spreads to adjacent air");
}

static void TestWaterOnGrassSpreadsIsolated(
    const std::shared_ptr<cutum::UBlockDefinitionStorage> &definitions,
    cutum::UFluidSpreadSystem &fluid)
{
  cutum::UBlockWorld world;
  world.SetFluidDefinitions(definitions.get());
  const glm::ivec3 grass_pos(4, 11, 4);
  const glm::ivec3 air_pos(5, 11, 4);
  for (const glm::ivec3 pos : {grass_pos, air_pos})
  {
    world.SetBlock(pos + glm::ivec3(0, -1, 0), kStone);
  }
  world.SetBlock(grass_pos, kTallGrass);
  world.SetBlock(air_pos, cutum::BLOCK_AIR);
  cutum::UFluidSpreadSystem::ApplyFluidFill(
      world, *definitions, grass_pos, kWater,
      cutum::FluidCellState::Source().WithKind(cutum::FluidKind::Water));

  cutum::UFluidUpdateSet queue;
  queue.Enqueue(grass_pos);
  queue.Enqueue(air_pos);
  FluidTest::RunQueueTicks(world, *definitions, queue, fluid, 1);

  FluidTest::Expect(world.GetBlock(grass_pos) == kTallGrass, kTestName,
                    "isolated waterlogged grass keeps decor");
  FluidTest::Expect(world.GetBlock(air_pos) == kWater, kTestName,
                    "isolated waterlogged grass spreads to adjacent air");
}

static void TestLavaOnGrassSpreadsIsolated(
    const std::shared_ptr<cutum::UBlockDefinitionStorage> &definitions,
    cutum::UFluidSpreadSystem &fluid)
{
  cutum::UBlockWorld world;
  world.SetFluidDefinitions(definitions.get());
  const glm::ivec3 grass_pos(6, 11, 4);
  const glm::ivec3 air_pos(7, 11, 4);
  for (const glm::ivec3 pos : {grass_pos, air_pos})
  {
    world.SetBlock(pos + glm::ivec3(0, -1, 0), kStone);
  }
  world.SetBlock(grass_pos, kTallGrass);
  world.SetBlock(air_pos, cutum::BLOCK_AIR);
  cutum::UFluidSpreadSystem::ApplyFluidFill(
      world, *definitions, grass_pos, kLava,
      cutum::FluidCellState::Source().WithKind(cutum::FluidKind::Lava));

  cutum::UFluidUpdateSet queue;
  queue.Enqueue(grass_pos);
  queue.Enqueue(air_pos);
  FluidTest::RunQueueTicks(world, *definitions, queue, fluid, 1);

  FluidTest::Expect(world.GetBlock(grass_pos) == kTallGrass, kTestName,
                    "isolated lava-waterlogged grass keeps decor");
  FluidTest::Expect(world.GetBlock(air_pos) == kLava, kTestName,
                    "isolated lava-waterlogged grass spreads to adjacent air");
}

static void TestShoreGrassNotLava(
    const std::shared_ptr<cutum::UBlockDefinitionStorage> &definitions)
{
  cutum::UBlockWorld world;
  world.SetFluidDefinitions(definitions.get());
  const glm::ivec3 grass_pos(1, 11, 0);
  world.SetBlock(glm::ivec3(0, 10, 0), kWater);
  world.SetFluidState(glm::ivec3(0, 10, 0),
                      cutum::FluidCellState::Source().WithKind(
                          cutum::FluidKind::Water));
  world.SetBlock(grass_pos, kTallGrass);
  world.SetFluidState(grass_pos,
                      cutum::FluidCellState::Flowing(1).WithKind(
                          cutum::FluidKind::Water));

  const cutum::BlockId kind = cutum::UFluidSpreadSystem::ResolveFluidBlockId(
      world, *definitions, grass_pos);
  FluidTest::Expect(kind == kWater, kTestName,
                    "shore decor resolves water block id");
  FluidTest::Expect(world.GetBlock(grass_pos) != kLava, kTestName,
                    "shore decor is not a lava block");
}

} // namespace

int main()
{
  const auto definitions = FluidTest::MakeTestWaterLavaDecorDefinitions();
  cutum::UBlockRegistry registry(nullptr, definitions);
  cutum::UFluidSpreadSystem fluid;

  TestLavaOnAirSpreads(definitions, fluid);
  TestLavaOnGrassWaterlogs(definitions, registry);
  TestWaterOnGrassSpreads(definitions, fluid);
  TestWaterOnGrassSpreadsIsolated(definitions, fluid);
  TestLavaOnGrassSpreadsIsolated(definitions, fluid);
  TestShoreGrassNotLava(definitions);

  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}
