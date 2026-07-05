#include "Test/FluidTestHelpers.h"

#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"
#include "World/Objects/ObjectUtil.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include "WorldGen/Core/WorldGenContext.h"
#include "WorldGen/Stages/WorldGenStages.h"

#include <iostream>
#include <memory>
#include <unordered_map>

namespace
{

constexpr const char *kTestName = "worldgen_fluid_vegetation_pipeline_test";
constexpr cutum::BlockId kStone = 8;
constexpr cutum::BlockId kWater = 9;
constexpr cutum::BlockId kLog = 20;
constexpr cutum::BlockId kLeaves = 21;
constexpr cutum::BlockId kGrass = 22;
constexpr int kSea = 48;

static std::shared_ptr<cutum::UBlockDefinitionStorage> MakeDefinitions()
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
  cutum::BlockDefinition log;
  log.Name = "tree_log";
  log.Physics = cutum::BlockPhysicsProfile::Solid();
  cutum::BlockDefinition leaves;
  leaves.Name = "tree_leaves";
  leaves.Physics = cutum::BlockPhysicsProfile::Solid();
  leaves.Physics.Movement.Occupancy = 0.0f;
  leaves.Render.Style = cutum::BlockRenderStyle::Cutout;
  leaves.Render.Transparent = true;
  cutum::BlockDefinition grass;
  grass.Name = "grass";
  grass.Physics = cutum::BlockPhysicsProfile::Solid();
  std::unordered_map<cutum::BlockId, cutum::BlockDefinition> by_id;
  by_id[kStone] = stone;
  by_id[kWater] = water;
  by_id[kLog] = log;
  by_id[kLeaves] = leaves;
  by_id[kGrass] = grass;
  std::unordered_map<std::string, cutum::BlockId> name_to_id;
  name_to_id["stone"] = kStone;
  name_to_id["water"] = kWater;
  name_to_id["tree_log"] = kLog;
  name_to_id["tree_leaves"] = kLeaves;
  name_to_id["grass"] = kGrass;
  definitions->ReplaceAll(std::move(by_id), std::move(name_to_id));
  return definitions;
}

static cutum::WorldObjectDefinition MakeGroundedTree()
{
  cutum::WorldObjectDefinition object;
  object.PlacementMode = cutum::ObjectPlacementMode::VerticalPlant;
  object.anchor = glm::ivec3(0);
  object.voxels.push_back({glm::ivec3(0, 0, 0), kLog});
  object.voxels.push_back({glm::ivec3(0, 1, 0), kLog});
  object.voxels.push_back({glm::ivec3(0, 2, 0), kLeaves});
  return object;
}

static cutum::WorldObjectDefinition MakePath3x3()
{
  cutum::WorldObjectDefinition object;
  object.PlacementMode = cutum::ObjectPlacementMode::SurfaceLayer;
  object.anchor = glm::ivec3(0);
  for (int dx = -1; dx <= 1; ++dx)
  {
    for (int dz = -1; dz <= 1; ++dz)
    {
      object.voxels.push_back({glm::ivec3(dx, 0, dz), kStone});
    }
  }
  return object;
}

static void FillCoastalColumn(cutum::UBlockWorld &world, int x, int z)
{
  for (int y = 0; y <= 47; ++y)
  {
    world.SetBlock(glm::ivec3(x, y, z), kStone);
  }
  world.SetBlock(glm::ivec3(x, 48, z), kWater);
  world.SetFluidState(glm::ivec3(x, 48, z),
                      cutum::FluidCellState::Source().WithKind(
                          cutum::FluidKind::Water));
  world.SetBlock(glm::ivec3(x, 49, z), kGrass);
  world.SetBlock(glm::ivec3(x, 50, z), kGrass);
  world.SetBlock(glm::ivec3(x, 51, z), kGrass);
}

static void TestSealPrunePipeline()
{
  cutum::UBlockWorld world;
  cutum::UBlockRegistry registry(nullptr, MakeDefinitions());
  cutum::ProceduralSettings settings;
  settings.FillWater = true;
  settings.SeaLevel = kSea;
  cutum::WorldGenContext ctx(world, registry, settings);
  ctx.Blocks.Stone = kStone;
  ctx.Blocks.Water = kWater;
  ctx.Blocks.Grass = kGrass;

  for (int x = 0; x < cutum::CHUNK_SIZE; ++x)
  {
    for (int z = 0; z < cutum::CHUNK_SIZE; ++z)
    {
      FillCoastalColumn(world, x, z);
      world.SetBlock(glm::ivec3(x, 56, z), kLeaves);
    }
  }

  cutum::SealFluidPocketsInChunk(ctx, 0, 0);
  cutum::PruneFloatingVegetationInChunk(ctx, 0, 0);

  FluidTest::Expect(world.GetBlock(glm::ivec3(4, 56, 4)) == cutum::BLOCK_AIR,
                    kTestName, "prune removes floating leaves after seal");
  FluidTest::Expect(world.GetBlock(glm::ivec3(4, 51, 4)) == kGrass, kTestName,
                    "prune keeps surface grass");
}

static void TestSurfacePathAfterSeal()
{
  cutum::UBlockWorld world;
  cutum::UBlockRegistry registry(nullptr, MakeDefinitions());
  for (int dx = -1; dx <= 1; ++dx)
  {
    for (int dz = -1; dz <= 1; ++dz)
    {
      FillCoastalColumn(world, dx, dz);
    }
  }

  cutum::ProceduralSettings settings;
  settings.FillWater = true;
  settings.SeaLevel = kSea;
  cutum::WorldGenContext ctx(world, registry, settings);
  ctx.Blocks.Stone = kStone;
  ctx.Blocks.Water = kWater;
  ctx.Blocks.Grass = kGrass;

  cutum::SealFluidPocketsInChunk(ctx, 0, 0);

  const cutum::WorldObjectDefinition path = MakePath3x3();
  const glm::ivec3 anchor(0, 51, 0);
  FluidTest::Expect(
      cutum::CanPlaceObjectAtForWorldGen(world, registry, path, anchor, 80,
                                         kSea),
      kTestName, "3x3 path accepted on grass after fluid seal");
  cutum::PlaceObjectAt(world, path, anchor, false);
  FluidTest::Expect(world.GetBlock(glm::ivec3(0, 51, 0)) == kStone, kTestName,
                    "path center placed on surface");
}

static void TestFillPlaceTreeSealPrune()
{
  cutum::UBlockWorld world;
  cutum::UBlockRegistry registry(nullptr, MakeDefinitions());
  FillCoastalColumn(world, 4, 4);

  cutum::ProceduralSettings settings;
  settings.FillWater = true;
  settings.SeaLevel = kSea;
  cutum::WorldGenContext ctx(world, registry, settings);
  ctx.Blocks.Stone = kStone;
  ctx.Blocks.Water = kWater;
  ctx.Blocks.Grass = kGrass;

  cutum::FillFluidColumn(ctx, 4, 4, 51);

  const cutum::WorldObjectDefinition tree = MakeGroundedTree();
  const glm::ivec3 anchor(4, 52, 4);
  FluidTest::Expect(
      cutum::CanPlaceObjectAtForWorldGen(world, registry, tree, anchor, 80,
                                         kSea),
      kTestName, "grounded tree accepted before seal");
  cutum::PlaceObjectAt(world, tree, anchor, false);
  FluidTest::Expect(world.GetBlock(glm::ivec3(4, 52, 4)) == kLog, kTestName,
                    "tree trunk placed above coastal grass");

  cutum::SealFluidPocketsInChunk(ctx, 0, 0);
  cutum::PruneFloatingVegetationInChunk(ctx, 0, 0);

  FluidTest::Expect(world.GetBlock(glm::ivec3(4, 52, 4)) == kLog, kTestName,
                    "seal and prune keep grounded trunk");
  FluidTest::Expect(world.GetBlock(glm::ivec3(4, 51, 4)) == kGrass, kTestName,
                    "seal and prune keep surface grass");
}

} // namespace

int main()
{
  TestSealPrunePipeline();
  TestSurfacePathAfterSeal();
  TestFillPlaceTreeSealPrune();
  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}
