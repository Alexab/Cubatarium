#include "Test/FluidTestHelpers.h"

#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"
#include "World/Math/FluidCellState.h"
#include "World/Physics/FluidSpreadSystem.h"
#include "World/Physics/FluidUpdateSet.h"

#include <iostream>
#include <memory>
#include <unordered_map>

namespace
{

constexpr const char *kTestName = "fluid_sim_mesh_integration_test";
constexpr cutum::BlockId kStone = 8;
constexpr cutum::BlockId kWater = 9;

static void BuildPit22(cutum::UBlockWorld &world)
{
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
      world.SetBlock(glm::ivec3(x, 11, z), cutum::BLOCK_AIR);
    }
  }
}

static void TestPit22CenterSource(
    const std::shared_ptr<cutum::UBlockDefinitionStorage> &definitions,
    cutum::UBlockRegistry &registry, cutum::UFluidSpreadSystem &fluid)
{
  cutum::UBlockWorld world;
  world.SetFluidDefinitions(definitions.get());
  BuildPit22(world);
  world.SetBlock(glm::ivec3(1, 10, 1), kWater);
  world.SetFluidState(glm::ivec3(1, 10, 1), cutum::FluidCellState::Source());

  cutum::UFluidUpdateSet queue;
  queue.Enqueue(glm::ivec3(1, 10, 1));
  FluidTest::RunQueueTicks(world, *definitions, queue, fluid, 500);

  std::vector<FluidTest::FluidCellExpectation> cells;
  for (int x = 1; x <= 2; ++x)
  {
    for (int z = 1; z <= 2; ++z)
    {
      cells.push_back({glm::ivec3(x, 10, z), true, 1, 0, false});
    }
  }
  FluidTest::ExpectFluidCells(kTestName, world, registry, kWater, cells);
}

static void TestPit22CornerSource(
    const std::shared_ptr<cutum::UBlockDefinitionStorage> &definitions,
    cutum::UBlockRegistry &registry, cutum::UFluidSpreadSystem &fluid)
{
  cutum::UBlockWorld world;
  world.SetFluidDefinitions(definitions.get());
  BuildPit22(world);
  world.SetBlock(glm::ivec3(1, 10, 1), kWater);
  world.SetFluidState(glm::ivec3(1, 10, 1), cutum::FluidCellState::Source());

  cutum::UFluidUpdateSet queue;
  queue.Enqueue(glm::ivec3(1, 10, 1));
  FluidTest::RunQueueTicks(world, *definitions, queue, fluid, 500);

  std::vector<FluidTest::FluidCellExpectation> cells;
  for (int x = 1; x <= 2; ++x)
  {
    for (int z = 1; z <= 2; ++z)
    {
      cells.push_back({glm::ivec3(x, 10, z), true, 1, 0, false});
    }
  }
  FluidTest::ExpectFluidCells(kTestName, world, registry, kWater, cells);
}

static void TestShoreFlat(
    const std::shared_ptr<cutum::UBlockDefinitionStorage> &definitions,
    cutum::UBlockRegistry &registry, cutum::UFluidSpreadSystem &fluid)
{
  cutum::UBlockWorld world;
  world.SetFluidDefinitions(definitions.get());
  world.SetBlock(glm::ivec3(0, 10, 0), kWater);
  world.SetFluidState(glm::ivec3(0, 10, 0), cutum::FluidCellState::Source());
  world.SetBlock(glm::ivec3(1, 10, 0), kStone);
  world.SetBlock(glm::ivec3(1, 10, 0), cutum::BLOCK_AIR);
  world.SetBlock(glm::ivec3(0, 11, 0), cutum::BLOCK_AIR);
  world.SetBlock(glm::ivec3(1, 11, 0), cutum::BLOCK_AIR);

  cutum::UFluidUpdateSet queue;
  queue.Enqueue(glm::ivec3(0, 10, 0));
  queue.Enqueue(glm::ivec3(1, 10, 0));
  FluidTest::RunQueueTicks(world, *definitions, queue, fluid, 200);

  FluidTest::Expect(world.GetBlock(glm::ivec3(1, 10, 0)) == kWater, kTestName,
                    "shore air fills from adjacent water");
  const std::vector<cutum::GreedyQuad> shore_quads =
      FluidTest::BuildFluidMesh(world, registry, glm::ivec3(1, 10, 0));
  int top_faces = 0;
  for (const cutum::GreedyQuad &quad : shore_quads)
  {
    if (quad.Id == kWater && quad.axis == 1 && quad.faceSign > 0)
    {
      ++top_faces;
    }
  }
  FluidTest::Expect(top_faces >= 1, kTestName, "shore scene has water top faces");
  FluidTest::Expect(FluidTest::CountVerticalFluidFaces(shore_quads, kWater) >= 1,
                    kTestName, "shore scene has vertical water faces");
  FluidTest::Expect(!world.IsAir(glm::ivec3(1, 10, 0)), kTestName,
                    "shore water blocks placement");
  FluidTest::Expect(!world.IsAir(glm::ivec3(0, 10, 0)), kTestName,
                    "source shore blocks placement");
}

static void TestShoreStair(
    const std::shared_ptr<cutum::UBlockDefinitionStorage> &definitions,
    cutum::UBlockRegistry &registry, cutum::UFluidSpreadSystem &fluid)
{
  cutum::UBlockWorld world;
  world.SetFluidDefinitions(definitions.get());
  world.SetBlock(glm::ivec3(0, 11, 0), kWater);
  world.SetFluidState(glm::ivec3(0, 11, 0), cutum::FluidCellState::Source());
  world.SetBlock(glm::ivec3(0, 10, 0), kWater);
  world.SetFluidState(glm::ivec3(0, 10, 0), cutum::FluidCellState::Source());
  world.SetBlock(glm::ivec3(1, 10, 0), cutum::BLOCK_AIR);
  world.SetBlock(glm::ivec3(0, 9, 0), cutum::BLOCK_AIR);
  world.SetBlock(glm::ivec3(1, 9, 0), cutum::BLOCK_AIR);
  world.SetBlock(glm::ivec3(0, 8, 0), kStone);
  world.SetBlock(glm::ivec3(1, 8, 0), kStone);

  cutum::UFluidUpdateSet queue;
  FluidTest::EnqueueFluidFrontier(queue, world, *definitions, glm::ivec3(1, 10, 0));
  FluidTest::RunQueueTicks(world, *definitions, queue, fluid, 200);

  FluidTest::Expect(world.GetBlock(glm::ivec3(1, 10, 0)) == kWater, kTestName,
                    "stair-step shore cavity fills");
  const std::vector<cutum::GreedyQuad> stair_quads =
      FluidTest::BuildFluidMesh(world, registry, glm::ivec3(1, 10, 0));
  int top_faces = 0;
  int vertical_faces = 0;
  for (const cutum::GreedyQuad &quad : stair_quads)
  {
    if (quad.Id != kWater)
    {
      continue;
    }
    if (quad.axis == 1 && quad.faceSign > 0)
    {
      ++top_faces;
    }
    else if (quad.axis != 1)
    {
      ++vertical_faces;
    }
  }
  FluidTest::Expect(top_faces + vertical_faces >= 1, kTestName,
                    "stair shore scene has visible water faces");
  FluidTest::Expect(vertical_faces >= 1, kTestName,
                    "stair shore scene has vertical water faces");
  FluidTest::Expect(!world.IsAir(glm::ivec3(1, 10, 0)), kTestName,
                    "stair cavity blocks placement");
}

static void TestDigGap(
    const std::shared_ptr<cutum::UBlockDefinitionStorage> &definitions,
    cutum::UBlockRegistry &registry, cutum::UFluidSpreadSystem &fluid)
{
  cutum::UBlockWorld world;
  world.SetFluidDefinitions(definitions.get());
  world.SetBlock(glm::ivec3(0, 10, 0), kWater);
  world.SetFluidState(glm::ivec3(0, 10, 0), cutum::FluidCellState::Source());
  world.SetBlock(glm::ivec3(1, 10, 0), kStone);
  world.SetBlock(glm::ivec3(2, 10, 0), cutum::BLOCK_AIR);
  world.SetBlock(glm::ivec3(1, 10, 0), cutum::BLOCK_AIR);

  cutum::UFluidUpdateSet queue;
  queue.Enqueue(glm::ivec3(0, 10, 0));
  FluidTest::EnqueueFluidFrontier(queue, world, *definitions, glm::ivec3(1, 10, 0));
  FluidTest::RunQueueTicks(world, *definitions, queue, fluid, 200);

  FluidTest::Expect(world.GetBlock(glm::ivec3(1, 10, 0)) == kWater, kTestName,
                    "dig gap fills after spread");
  FluidTest::Expect(!world.IsAir(glm::ivec3(1, 10, 0)), kTestName,
                    "dug gap cell blocks placement");
  const std::vector<cutum::GreedyQuad> gap_quads =
      FluidTest::BuildFluidMesh(world, registry, glm::ivec3(1, 10, 0));
  int visible_faces = 0;
  for (const cutum::GreedyQuad &quad : gap_quads)
  {
    if (quad.Id == kWater)
    {
      ++visible_faces;
    }
  }
  FluidTest::Expect(visible_faces >= 1, kTestName,
                    "dig gap water cell has mesh faces");
  FluidTest::Expect(
      FluidTest::CountVerticalFluidFaces(gap_quads, kWater) >= 1, kTestName,
      "dig gap scene has vertical water face to air");
}

static void TestWaterloggedGrassMesh(
    const std::shared_ptr<cutum::UBlockDefinitionStorage> &definitions,
    cutum::UBlockRegistry &registry)
{
  constexpr cutum::BlockId kTallGrass = 10;
  cutum::UBlockWorld world;
  world.SetFluidDefinitions(definitions.get());
  world.SetBlock(glm::ivec3(0, 10, 0), kStone);
  world.SetBlock(glm::ivec3(0, 11, 0), kTallGrass);
  world.SetFluidState(glm::ivec3(0, 11, 0),
                      cutum::FluidCellState::Flowing(1).WithKind(
                          cutum::FluidKind::Water));

  FluidTest::Expect(world.GetBlock(glm::ivec3(0, 11, 0)) == kTallGrass, kTestName,
                    "waterlogged grass block id");
  FluidTest::Expect(
      cutum::PackFluidCellState(world.GetFluidState(glm::ivec3(0, 11, 0))) != 0,
      kTestName, "waterlogged grass fluid_data");
  FluidTest::Expect(registry.IsFluidPermeable(kTallGrass), kTestName,
                    "tall_grass is fluid permeable");

  const glm::ivec3 pos(0, 11, 0);
  const glm::ivec3 chunk_coord = cutum::UChunkManager::WorldToChunk(pos);
  const std::vector<cutum::GreedyQuad> quads =
      FluidTest::BuildFluidMesh(world, registry, pos);
  FluidTest::Expect(FluidTest::CountTopFacesAt(quads, kWater, pos, chunk_coord) >=
                        1,
                    kTestName,                     "waterlogged grass has water top mesh face");
}

static void TestOpaqueColumnBarrierBetweenFluids(
    cutum::UBlockRegistry &registry)
{
  auto definitions = std::make_shared<cutum::UBlockDefinitionStorage>();
  constexpr cutum::BlockId kWater = 9;
  constexpr cutum::BlockId kLava = 11;
  constexpr cutum::BlockId kLog = 12;
  cutum::BlockDefinition water;
  water.Name = "water";
  water.Physics = cutum::BlockPhysicsProfile::FromPreset("water");
  water.Render.Transparent = true;
  water.Render.Style = cutum::BlockRenderStyle::Fluid;
  cutum::BlockDefinition lava;
  lava.Name = "lava";
  lava.Physics = cutum::BlockPhysicsProfile::FromPreset("lava");
  lava.Render.Transparent = true;
  lava.Render.Style = cutum::BlockRenderStyle::Fluid;
  cutum::BlockDefinition log;
  log.Name = "tree_log";
  log.Physics = cutum::BlockPhysicsProfile::Solid();
  std::unordered_map<cutum::BlockId, cutum::BlockDefinition> by_id;
  by_id[kWater] = water;
  by_id[kLava] = lava;
  by_id[kLog] = log;
  std::unordered_map<std::string, cutum::BlockId> name_to_id;
  name_to_id["water"] = kWater;
  name_to_id["lava"] = kLava;
  name_to_id["tree_log"] = kLog;
  definitions->ReplaceAll(std::move(by_id), std::move(name_to_id));
  cutum::UBlockRegistry stack_registry(nullptr, definitions);

  cutum::UBlockWorld world;
  world.SetFluidDefinitions(definitions.get());
  world.SetBlock(glm::ivec3(0, 12, 0), kWater);
  world.SetBlock(glm::ivec3(0, 11, 0), kLog);
  world.SetBlock(glm::ivec3(0, 10, 0), kLava);

  FluidTest::Expect(stack_registry.BlocksMovement(kLog), kTestName,
                    "log blocks movement between fluids");
  FluidTest::Expect(!stack_registry.IsLiquid(kLog), kTestName,
                    "log is opaque to fluid kind routing");
  FluidTest::Expect(world.GetBlock(glm::ivec3(0, 11, 0)) == kLog, kTestName,
                    "log remains between water and lava column");
  const std::vector<cutum::GreedyQuad> water_quads =
      FluidTest::BuildFluidMesh(world, stack_registry, glm::ivec3(0, 12, 0));
  FluidTest::Expect(!water_quads.empty(), kTestName,
                    "water column still emits mesh above log barrier");
  (void)registry;
}

} // namespace

int main()
{
  const auto definitions = FluidTest::MakeTestFluidDefinitions();
  const auto decor_definitions = FluidTest::MakeTestFluidDecorDefinitions();
  cutum::UBlockRegistry registry(nullptr, definitions);
  cutum::UBlockRegistry decor_registry(nullptr, decor_definitions);
  cutum::UFluidSpreadSystem fluid;

  TestPit22CenterSource(definitions, registry, fluid);
  TestPit22CornerSource(definitions, registry, fluid);
  TestShoreFlat(definitions, registry, fluid);
  TestShoreStair(definitions, registry, fluid);
  TestDigGap(definitions, registry, fluid);
  TestWaterloggedGrassMesh(decor_definitions, decor_registry);
  TestOpaqueColumnBarrierBetweenFluids(registry);

  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}
