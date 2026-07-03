#include "Test/FluidTestHelpers.h"

#include "Blocks/BlockRegistry.h"
#include "Render/Mesh/GreedyMesher.h"
#include "World/Core/BlockWorld.h"
#include "World/Math/FluidCellState.h"
#include "World/Objects/ObjectUtil.h"
#include "World/Physics/FluidSpreadSystem.h"
#include "World/Physics/FluidUpdateSet.h"

#include <iostream>
#include <memory>
#include <unordered_map>

namespace
{

constexpr const char *kTestName = "fluid_gameplay_fixes_test";
constexpr cutum::BlockId kStone = 8;
constexpr cutum::BlockId kWater = 9;
constexpr cutum::BlockId kLava = 11;
constexpr cutum::BlockId kTallGrass = 10;
constexpr cutum::BlockId kSand = 12;
constexpr cutum::BlockId kGrassBlock = 13;

static std::shared_ptr<cutum::UBlockDefinitionStorage> MakeGameplayDefinitions()
{
  auto definitions = std::make_shared<cutum::UBlockDefinitionStorage>();
  cutum::BlockDefinition stone;
  stone.Name = "stone";
  stone.Physics = cutum::BlockPhysicsProfile::Solid();
  cutum::BlockDefinition water;
  water.Name = "water";
  water.Physics = cutum::BlockPhysicsProfile::FromPreset("water");
  water.Render.Transparent = true;
  water.Render.Style = cutum::BlockRenderStyle::Fluid;
  cutum::BlockDefinition grass;
  grass.Name = "tall_grass";
  grass.Physics = cutum::BlockPhysicsProfile::Solid();
  grass.Physics.Movement.Occupancy = 0.0f;
  grass.Render.Style = cutum::BlockRenderStyle::Cross;
  grass.Render.Transparent = true;
  cutum::BlockDefinition sand;
  sand.Name = "sand";
  sand.Physics = cutum::BlockPhysicsProfile::Solid();
  cutum::BlockDefinition grass_block;
  grass_block.Name = "grass";
  grass_block.Physics = cutum::BlockPhysicsProfile::Solid();
  cutum::BlockDefinition lava;
  lava.Name = "lava";
  lava.Physics = cutum::BlockPhysicsProfile::FromPreset("lava");
  lava.Render.Transparent = true;
  lava.Render.Style = cutum::BlockRenderStyle::Fluid;
  std::unordered_map<cutum::BlockId, cutum::BlockDefinition> by_id;
  by_id[kStone] = stone;
  by_id[kWater] = water;
  by_id[kTallGrass] = grass;
  by_id[kSand] = sand;
  by_id[kGrassBlock] = grass_block;
  by_id[kLava] = lava;
  std::unordered_map<std::string, cutum::BlockId> name_to_id;
  name_to_id["stone"] = kStone;
  name_to_id["water"] = kWater;
  name_to_id["tall_grass"] = kTallGrass;
  name_to_id["sand"] = kSand;
  name_to_id["grass"] = kGrassBlock;
  name_to_id["lava"] = kLava;
  definitions->ReplaceAll(std::move(by_id), std::move(name_to_id));
  return definitions;
}

static cutum::FluidFloodOptions GameplayFloodOptions(cutum::BlockId water_id,
                                                     int sea_level = -1)
{
  cutum::FluidFloodOptions options;
  options.water_id = water_id;
  options.source_for_air = false;
  options.sea_level = sea_level;
  options.max_passes = 8;
  return options;
}

static void TestBreakRemovesDecorAbove()
{
  cutum::UBlockWorld world;
  cutum::UBlockRegistry registry(nullptr, FluidTest::MakeTestFluidDecorDefinitions());
  world.SetBlock(glm::ivec3(0, 10, 0), kStone);
  world.SetBlock(glm::ivec3(0, 11, 0), kTallGrass);

  const std::vector<glm::ivec3> broken =
      cutum::BreakUnsupportedBlocksAbove(world, registry, glm::ivec3(0, 10, 0));
  FluidTest::Expect(broken.size() == 1, kTestName, "grass removed with ground");
  FluidTest::Expect(world.GetBlock(glm::ivec3(0, 11, 0)) == cutum::BLOCK_AIR,
                    kTestName, "grass cell is air");
  FluidTest::Expect(world.GetBlock(glm::ivec3(0, 10, 0)) == kStone, kTestName,
                    "ground untouched");
}

static void TestBreakSandRemovesGrassSurfaceAndDecor()
{
  cutum::UBlockWorld world;
  cutum::UBlockRegistry registry(nullptr, MakeGameplayDefinitions());
  world.SetBlock(glm::ivec3(0, 10, 0), kStone);
  world.SetBlock(glm::ivec3(0, 11, 0), kSand);
  world.SetBlock(glm::ivec3(0, 12, 0), kGrassBlock);
  world.SetBlock(glm::ivec3(0, 13, 0), kTallGrass);

  const std::vector<glm::ivec3> broken =
      cutum::BreakUnsupportedBlocksAbove(world, registry, glm::ivec3(0, 11, 0));
  FluidTest::Expect(broken.size() == 2, kTestName,
                    "sand break removes grass surface and decor above");
  FluidTest::Expect(world.GetBlock(glm::ivec3(0, 12, 0)) == cutum::BLOCK_AIR,
                    kTestName, "grass surface removed with sand");
  FluidTest::Expect(world.GetBlock(glm::ivec3(0, 13, 0)) == cutum::BLOCK_AIR,
                    kTestName, "decor removed with sand");
}

static void TestFloodBelowSeaUsesWaterDespiteLavaNeighbor(
    const std::shared_ptr<cutum::UBlockDefinitionStorage> &definitions)
{
  cutum::UBlockWorld world;
  world.SetFluidDefinitions(definitions.get());
  world.SetBlock(glm::ivec3(0, 62, 0), kLava);
  world.SetFluidState(glm::ivec3(0, 62, 0), cutum::FluidCellState::Source());
  world.SetBlock(glm::ivec3(1, 63, 0), cutum::BLOCK_AIR);

  cutum::FluidFloodOptions options = GameplayFloodOptions(kWater, 63);
  const int filled = cutum::UFluidSpreadSystem::FloodBreakSiteFromWetNeighbors(
      world, *definitions, glm::ivec3(1, 63, 0), options);
  FluidTest::Expect(filled > 0, kTestName, "below-sea break flood fills");
  FluidTest::Expect(world.GetBlock(glm::ivec3(1, 63, 0)) == kWater, kTestName,
                    "below-sea break flood uses water not lava");
}

static void TestResolveFluidKindPrefersWater(
    const std::shared_ptr<cutum::UBlockDefinitionStorage> &definitions)
{
  cutum::UBlockWorld world;
  world.SetBlock(glm::ivec3(0, 63, 0), kLava);
  world.SetFluidState(glm::ivec3(0, 63, 0), cutum::FluidCellState::Source());
  world.SetBlock(glm::ivec3(1, 63, 0), kWater);
  world.SetFluidState(glm::ivec3(1, 63, 0), cutum::FluidCellState::Source());
  world.SetBlock(glm::ivec3(2, 63, 0), kTallGrass);
  world.SetFluidState(glm::ivec3(2, 63, 0), cutum::FluidCellState::Flowing(1));

  const cutum::BlockId kind = cutum::UFluidSpreadSystem::ResolveFluidKind(
      world, *definitions, glm::ivec3(2, 63, 0), kTallGrass);
  FluidTest::Expect(kind == kWater, kTestName,
                    "waterlogged decor resolves water over lava");
}

static void TestFloodSpillBelowSeaUsesWaterDespiteLavaNeighbor(
    const std::shared_ptr<cutum::UBlockDefinitionStorage> &definitions)
{
  cutum::UBlockWorld world;
  world.SetFluidDefinitions(definitions.get());
  world.SetBlock(glm::ivec3(0, 63, 0), kLava);
  world.SetFluidState(glm::ivec3(0, 63, 0), cutum::FluidCellState::Source());
  world.SetBlock(glm::ivec3(1, 63, 0), cutum::BLOCK_AIR);
  world.SetBlock(glm::ivec3(1, 64, 0), kTallGrass);

  cutum::FluidFloodOptions options = GameplayFloodOptions(kWater, 63);
  cutum::UFluidSpreadSystem::FloodBreakSiteFromWetNeighbors(
      world, *definitions, glm::ivec3(1, 64, 0), options);
  FluidTest::Expect(world.GetBlock(glm::ivec3(1, 63, 0)) == kWater, kTestName,
                    "spill below break site uses water not lava");
  FluidTest::Expect(world.GetBlock(glm::ivec3(1, 64, 0)) == kTallGrass,
                    kTestName, "shore decor preserved during spill flood");
  FluidTest::Expect(
      cutum::PackFluidCellState(world.GetFluidState(glm::ivec3(1, 64, 0))) != 0,
      kTestName, "shore decor waterlogged during spill flood");
}

static void TestGameplayFloodFillsBrokenCell(
    const std::shared_ptr<cutum::UBlockDefinitionStorage> &definitions)
{
  cutum::UBlockWorld world;
  world.SetFluidDefinitions(definitions.get());
  world.SetBlock(glm::ivec3(0, 10, 0), kStone);
  world.SetBlock(glm::ivec3(1, 10, 0), kWater);
  world.SetFluidState(glm::ivec3(1, 10, 0), cutum::FluidCellState::Source());
  world.SetBlock(glm::ivec3(0, 10, 0), cutum::BLOCK_AIR);

  const int filled = cutum::UFluidSpreadSystem::FloodBreakSiteFromWetNeighbors(
      world, *definitions, glm::ivec3(0, 10, 0), GameplayFloodOptions(kWater));
  FluidTest::Expect(filled > 0, kTestName, "gameplay flood fills broken cell");
  FluidTest::Expect(world.GetBlock(glm::ivec3(0, 10, 0)) == kWater, kTestName,
                    "broken cell becomes water");
  FluidTest::Expect(!world.GetFluidState(glm::ivec3(0, 10, 0)).IsSource(),
                    kTestName, "gameplay flood uses flowing not source");
}

static void TestGameplayFloodVerticalPit(
    const std::shared_ptr<cutum::UBlockDefinitionStorage> &definitions)
{
  cutum::UBlockWorld world;
  world.SetFluidDefinitions(definitions.get());
  for (int y = 8; y <= 12; ++y)
  {
    world.SetBlock(glm::ivec3(0, y, 0), kStone);
    world.SetBlock(glm::ivec3(2, y, 0), kStone);
    world.SetBlock(glm::ivec3(1, y, 0), kWater);
    world.SetFluidState(glm::ivec3(1, y, 0), cutum::FluidCellState::Source());
  }
  world.SetBlock(glm::ivec3(1, 7, 0), cutum::BLOCK_AIR);
  const int filled = cutum::UFluidSpreadSystem::FloodBreakSiteFromWetNeighbors(
      world, *definitions, glm::ivec3(1, 7, 0), GameplayFloodOptions(kWater));
  FluidTest::Expect(filled > 0, kTestName, "vertical pit flood applies");
  FluidTest::Expect(world.GetBlock(glm::ivec3(1, 7, 0)) == kWater, kTestName,
                    "dug floor under water column fills");
}

static void TestGameplayFloodShoreGrass(
    const std::shared_ptr<cutum::UBlockDefinitionStorage> &definitions)
{
  cutum::UBlockWorld world;
  world.SetFluidDefinitions(definitions.get());
  world.SetBlock(glm::ivec3(0, 10, 0), kWater);
  world.SetFluidState(glm::ivec3(0, 10, 0), cutum::FluidCellState::Source());
  world.SetBlock(glm::ivec3(1, 10, 0), cutum::BLOCK_AIR);
  world.SetBlock(glm::ivec3(1, 11, 0), kTallGrass);

  cutum::UFluidSpreadSystem::FloodBreakSiteFromWetNeighbors(
      world, *definitions, glm::ivec3(1, 10, 0), GameplayFloodOptions(kWater));

  FluidTest::Expect(world.GetBlock(glm::ivec3(1, 10, 0)) == kWater, kTestName,
                    "shore pocket air becomes water");
  FluidTest::Expect(world.GetBlock(glm::ivec3(1, 11, 0)) == kTallGrass,
                    kTestName, "shore grass block preserved");
  FluidTest::Expect(
      PackFluidCellState(world.GetFluidState(glm::ivec3(1, 11, 0))) != 0,
      kTestName, "shore grass waterlogged");
}

static void TestWaterPriorityOverLava(
    const std::shared_ptr<cutum::UBlockDefinitionStorage> &definitions)
{
  cutum::UBlockWorld world;
  world.SetFluidDefinitions(definitions.get());
  world.SetBlock(glm::ivec3(0, 10, 0), kWater);
  world.SetFluidState(glm::ivec3(0, 10, 0), cutum::FluidCellState::Source());
  world.SetBlock(glm::ivec3(0, 9, 0), kLava);
  world.SetFluidState(glm::ivec3(0, 9, 0), cutum::FluidCellState::Source());
  world.SetBlock(glm::ivec3(1, 10, 0), cutum::BLOCK_AIR);

  cutum::UFluidSpreadSystem::FloodBreakSiteFromWetNeighbors(
      world, *definitions, glm::ivec3(1, 10, 0), GameplayFloodOptions(kWater));

  FluidTest::Expect(world.GetBlock(glm::ivec3(1, 10, 0)) == kWater, kTestName,
                    "water neighbor wins over lava in gameplay flood");
}

static void TestCellTouchesWetAir(
    const std::shared_ptr<cutum::UBlockDefinitionStorage> &definitions)
{
  cutum::UBlockWorld world;
  world.SetBlock(glm::ivec3(0, 10, 0), kWater);
  world.SetFluidState(glm::ivec3(0, 10, 0), cutum::FluidCellState::Source());
  world.SetBlock(glm::ivec3(2, 10, 0), kWater);
  world.SetFluidState(glm::ivec3(2, 10, 0), cutum::FluidCellState::Source());
  world.SetBlock(glm::ivec3(1, 10, 0), cutum::BLOCK_AIR);
  FluidTest::Expect(
      cutum::UFluidSpreadSystem::CellTouchesWet(world, *definitions,
                                                glm::ivec3(1, 10, 0)),
      kTestName, "air between water blocks touches wet");
}

static void TestBreakSiteDoesNotFillLargeAirPocket(
    const std::shared_ptr<cutum::UBlockDefinitionStorage> &definitions)
{
  cutum::UBlockWorld world;
  world.SetBlock(glm::ivec3(0, 10, 0), kWater);
  world.SetFluidState(glm::ivec3(0, 10, 0), cutum::FluidCellState::Source());
  for (int dx = 1; dx <= 6; ++dx)
  {
    for (int dy = 0; dy <= 4; ++dy)
    {
      for (int dz = -2; dz <= 2; ++dz)
      {
        world.SetBlock(glm::ivec3(dx, 10 + dy, dz), cutum::BLOCK_AIR);
      }
    }
  }
  world.SetBlock(glm::ivec3(3, 10, 0), kStone);
  world.SetBlock(glm::ivec3(3, 10, 0), cutum::BLOCK_AIR);

  const int filled = cutum::UFluidSpreadSystem::FloodBreakSiteFromWetNeighbors(
      world, *definitions, glm::ivec3(3, 10, 0), GameplayFloodOptions(kWater));
  FluidTest::Expect(filled <= 8, kTestName, "break-site flood stays local");
  FluidTest::Expect(world.GetBlock(glm::ivec3(3, 10, 0)) == kWater, kTestName,
                    "broken cell fills");
  FluidTest::Expect(world.GetBlock(glm::ivec3(6, 12, 0)) == cutum::BLOCK_AIR,
                    kTestName, "distant air in pocket stays dry");
}

static void TestTrenchSpread(
    const std::shared_ptr<cutum::UBlockDefinitionStorage> &definitions,
    cutum::UBlockRegistry &registry, cutum::UFluidSpreadSystem &fluid)
{
  cutum::UBlockWorld world;
  world.SetFluidDefinitions(definitions.get());
  for (int x = 0; x < 8; ++x)
  {
    world.SetBlock(glm::ivec3(x, 9, 0), kStone);
  }
  world.SetBlock(glm::ivec3(0, 10, 0), kWater);
  world.SetFluidState(glm::ivec3(0, 10, 0), cutum::FluidCellState::Source());
  for (int x = 1; x <= 6; ++x)
  {
    world.SetBlock(glm::ivec3(x, 10, 0), cutum::BLOCK_AIR);
  }

  cutum::UFluidUpdateSet queue;
  queue.Enqueue(glm::ivec3(0, 10, 0));
  for (int x = 1; x <= 6; ++x)
  {
    queue.Enqueue(glm::ivec3(x, 10, 0));
  }
  FluidTest::RunQueueTicks(world, *definitions, queue, fluid, 400);

  for (int x = 1; x <= 6; ++x)
  {
    FluidTest::Expect(world.GetBlock(glm::ivec3(x, 10, 0)) == kWater, kTestName,
                      "straight trench cell fills with water");
  }
}

static void TestSourceFlowingMeshNoInternalFace(
    const std::shared_ptr<cutum::UBlockDefinitionStorage> &definitions)
{
  cutum::UBlockRegistry registry(nullptr, definitions);
  cutum::UBlockWorld world;
  world.SetFluidDefinitions(definitions.get());
  world.SetBlock(glm::ivec3(5, 10, 5), kWater);
  world.SetFluidState(glm::ivec3(5, 10, 5), cutum::FluidCellState::Source());
  world.SetBlock(glm::ivec3(6, 10, 5), kWater);
  world.SetFluidState(glm::ivec3(6, 10, 5), cutum::FluidCellState::Flowing(1));
  world.SetBlock(glm::ivec3(5, 11, 5), cutum::BLOCK_AIR);
  world.SetBlock(glm::ivec3(6, 11, 5), cutum::BLOCK_AIR);

  const std::vector<cutum::GreedyQuad> quads =
      cutum::UGreedyMesher::BuildChunkMesh(world, glm::ivec3(0, 0, 0), registry);
  int internal_faces = 0;
  for (const cutum::GreedyQuad &quad : quads)
  {
    if (quad.Id != kWater || quad.axis != 0 || quad.faceSign <= 0 ||
        quad.slice != 5)
    {
      continue;
    }
    ++internal_faces;
  }
  FluidTest::Expect(internal_faces == 0,
                    kTestName, "source+flowing hide internal mesh face");
}

} // namespace

int main()
{
  TestBreakRemovesDecorAbove();
  TestBreakSandRemovesGrassSurfaceAndDecor();

  const auto definitions = MakeGameplayDefinitions();
  cutum::UBlockRegistry registry(nullptr, definitions);
  cutum::UFluidSpreadSystem fluid;

  TestGameplayFloodFillsBrokenCell(definitions);
  TestGameplayFloodVerticalPit(definitions);
  TestGameplayFloodShoreGrass(definitions);
  TestWaterPriorityOverLava(definitions);
  TestFloodBelowSeaUsesWaterDespiteLavaNeighbor(definitions);
  TestResolveFluidKindPrefersWater(definitions);
  TestFloodSpillBelowSeaUsesWaterDespiteLavaNeighbor(definitions);
  TestCellTouchesWetAir(definitions);
  TestBreakSiteDoesNotFillLargeAirPocket(definitions);
  TestSourceFlowingMeshNoInternalFace(definitions);
  TestTrenchSpread(definitions, registry, fluid);

  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}
