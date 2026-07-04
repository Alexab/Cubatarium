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

constexpr const char *kTestName = "worldgen_vegetation_placement_test";
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

static void FillCoastalColumnAt(cutum::UBlockWorld &world, int x, int z)
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

static void FillCoastalColumn(cutum::UBlockWorld &world)
{
  FillCoastalColumnAt(world, 0, 0);
}

static cutum::WorldObjectDefinition MakeTreeObject(bool include_logs)
{
  cutum::WorldObjectDefinition object;
  object.PlacementMode = cutum::ObjectPlacementMode::VerticalPlant;
  object.anchor = glm::ivec3(0);
  if (include_logs)
  {
    object.voxels.push_back({glm::ivec3(0, 0, 0), kLog});
    object.voxels.push_back({glm::ivec3(0, 1, 0), kLog});
    object.voxels.push_back({glm::ivec3(0, 2, 0), kLog});
  }
  object.voxels.push_back({glm::ivec3(0, 3, 0), kLeaves});
  return object;
}

static cutum::WorldObjectDefinition MakeEmbeddedRootTree()
{
  cutum::WorldObjectDefinition object;
  object.PlacementMode = cutum::ObjectPlacementMode::VerticalPlant;
  object.anchor = glm::ivec3(0);
  object.voxels.push_back({glm::ivec3(0, -1, 0), kLog});
  object.voxels.push_back({glm::ivec3(0, 0, 0), kLog});
  object.voxels.push_back({glm::ivec3(0, 1, 0), kLog});
  object.voxels.push_back({glm::ivec3(0, 2, 0), kLog});
  object.voxels.push_back({glm::ivec3(0, 3, 0), kLeaves});
  return object;
}

static cutum::WorldObjectDefinition MakeBranchColumnTree()
{
  cutum::WorldObjectDefinition object;
  object.PlacementMode = cutum::ObjectPlacementMode::VerticalPlant;
  object.anchor = glm::ivec3(0);
  object.voxels.push_back({glm::ivec3(0, 0, 0), kLog});
  object.voxels.push_back({glm::ivec3(0, 1, 0), kLog});
  object.voxels.push_back({glm::ivec3(0, 2, 0), kLog});
  object.voxels.push_back({glm::ivec3(1, 3, 0), kLog});
  object.voxels.push_back({glm::ivec3(1, 4, 0), kLeaves});
  object.voxels.push_back({glm::ivec3(0, 3, 0), kLeaves});
  return object;
}

static cutum::WorldObjectDefinition MakePathCobble3x3()
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

static cutum::WorldObjectDefinition MakeFallenLog()
{
  cutum::WorldObjectDefinition object;
  object.PlacementMode = cutum::ObjectPlacementMode::SurfaceLayer;
  object.anchor = glm::ivec3(0);
  object.voxels.push_back({glm::ivec3(-1, 0, 0), kLog});
  object.voxels.push_back({glm::ivec3(0, 0, 0), kLog});
  object.voxels.push_back({glm::ivec3(1, 0, 0), kLog});
  return object;
}

static void TestResolvePlacementSurfaceOnCoast()
{
  cutum::UBlockWorld world;
  cutum::UBlockRegistry registry(nullptr, MakeDefinitions());
  FillCoastalColumn(world);

  const cutum::PlacementSurfaceInfo info =
      cutum::ResolvePlacementSurfaceY(world, registry, 0, 0, 51, 128, kSea);
  FluidTest::Expect(info.topSolidY == 51, kTestName,
                    "coastal column resolves top solid at grass surface");
  FluidTest::Expect(
      cutum::IsExposedLandSurface(world, registry, 0, 0, info.topSolidY),
      kTestName, "coastal top solid is exposed");
}

static void TestRejectLeavesWithoutTrunk()
{
  cutum::UBlockWorld world;
  cutum::UBlockRegistry registry(nullptr, MakeDefinitions());
  FillCoastalColumn(world);

  const cutum::WorldObjectDefinition leaves_only = MakeTreeObject(false);
  const glm::ivec3 anchor(0, 55, 0);
  FluidTest::Expect(
      !cutum::CanPlaceObjectAtForWorldGen(world, registry, leaves_only, anchor,
                                          80, kSea),
      kTestName, "leaves-only prefab rejected without trunk");
}

static void TestAcceptGroundedTree()
{
  cutum::UBlockWorld world;
  cutum::UBlockRegistry registry(nullptr, MakeDefinitions());
  FillCoastalColumn(world);

  const cutum::WorldObjectDefinition tree = MakeTreeObject(true);
  const glm::ivec3 anchor(0, 52, 0);
  FluidTest::Expect(cutum::CanPlaceObjectAtForWorldGen(world, registry, tree,
                                                       anchor, 80, kSea),
                    kTestName, "contiguous trunk on coastal surface accepted");
}

static void TestAcceptEmbeddedRootTree()
{
  cutum::UBlockWorld world;
  cutum::UBlockRegistry registry(nullptr, MakeDefinitions());
  FillCoastalColumn(world);

  const cutum::WorldObjectDefinition tree = MakeEmbeddedRootTree();
  const glm::ivec3 anchor(0, 52, 0);
  FluidTest::Expect(cutum::CanPlaceObjectAtForWorldGen(world, registry, tree,
                                                       anchor, 80, kSea),
                    kTestName, "embedded root log at surface accepted");
}

static void TestAcceptBranchColumnTree()
{
  cutum::UBlockWorld world;
  cutum::UBlockRegistry registry(nullptr, MakeDefinitions());
  FillCoastalColumnAt(world, 0, 0);
  FillCoastalColumnAt(world, 1, 0);

  const cutum::WorldObjectDefinition tree = MakeBranchColumnTree();
  const glm::ivec3 anchor(0, 52, 0);
  FluidTest::Expect(cutum::CanPlaceObjectAtForWorldGen(world, registry, tree,
                                                       anchor, 80, kSea),
                    kTestName, "branch-column log above trunk accepted");
}

static void TestAcceptSurfaceLayerPath()
{
  cutum::UBlockWorld world;
  cutum::UBlockRegistry registry(nullptr, MakeDefinitions());
  for (int dx = -1; dx <= 1; ++dx)
  {
    for (int dz = -1; dz <= 1; ++dz)
    {
      FillCoastalColumnAt(world, dx, dz);
    }
  }

  const cutum::WorldObjectDefinition path = MakePathCobble3x3();
  const glm::ivec3 anchor(0, 51, 0);
  FluidTest::Expect(cutum::CanPlaceObjectAtForWorldGen(world, registry, path,
                                                       anchor, 80, kSea),
                    kTestName, "3x3 surface path on grass accepted");
}

static void TestAcceptFallenLog()
{
  cutum::UBlockWorld world;
  cutum::UBlockRegistry registry(nullptr, MakeDefinitions());
  for (int dx = -1; dx <= 1; ++dx)
  {
    FillCoastalColumnAt(world, dx, 0);
  }

  const cutum::WorldObjectDefinition log = MakeFallenLog();
  const glm::ivec3 anchor(0, 51, 0);
  FluidTest::Expect(cutum::CanPlaceObjectAtForWorldGen(world, registry, log,
                                                       anchor, 80, kSea),
                    kTestName, "3x1 fallen log on grass accepted");
}

static void TestResolveSurfaceLayerAnchorY()
{
  cutum::UBlockRegistry registry(nullptr, MakeDefinitions());
  const cutum::WorldObjectDefinition path = MakePathCobble3x3();
  const cutum::WorldObjectDefinition tree = MakeTreeObject(true);
  FluidTest::Expect(cutum::ResolveWorldGenAnchorY(path, registry, 51, 0) == 51,
                    kTestName, "surface layer anchor sits on top solid");
  FluidTest::Expect(cutum::ResolveWorldGenAnchorY(tree, registry, 51, 0) == 52,
                    kTestName, "vertical tree anchor sits above top solid");
}

static void TestRejectMisTaggedVerticalPlantAsSurfaceLayer()
{
  cutum::UBlockWorld world;
  cutum::UBlockRegistry registry(nullptr, MakeDefinitions());
  FillCoastalColumn(world);

  cutum::WorldObjectDefinition tree = MakeTreeObject(true);
  tree.PlacementMode = cutum::ObjectPlacementMode::SurfaceLayer;
  const glm::ivec3 wrong_anchor(0, 51, 0);
  FluidTest::Expect(
      !cutum::CanPlaceObjectAtForWorldGen(world, registry, tree, wrong_anchor,
                                          80, kSea),
      kTestName, "multi-column tree rejected when tagged surface_layer");
}

static void TestPruneFloatingLeaves()
{
  cutum::UBlockWorld world;
  cutum::UBlockRegistry registry(nullptr, MakeDefinitions());
  FillCoastalColumn(world);
  world.SetBlock(glm::ivec3(0, 56, 0), kLeaves);

  const int removed =
      cutum::PruneFloatingPlantsInColumn(world, registry, 0, 0, 80);
  FluidTest::Expect(removed == 1, kTestName, "floating leaves pruned");
  FluidTest::Expect(world.GetBlock(glm::ivec3(0, 56, 0)) == cutum::BLOCK_AIR,
                    kTestName, "floating leaf cell cleared");
  FluidTest::Expect(world.GetBlock(glm::ivec3(0, 51, 0)) == kGrass, kTestName,
                    "surface grass preserved");
}

static void TestSealAndPruneCoastalChunk()
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
  ctx.Blocks.Dirt = kStone;

  for (int x = 0; x < cutum::CHUNK_SIZE; ++x)
  {
    for (int z = 0; z < cutum::CHUNK_SIZE; ++z)
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
      world.SetBlock(glm::ivec3(x, 56, z), kLeaves);
    }
  }

  cutum::PruneFloatingVegetationInChunk(ctx, 0, 0);
  FluidTest::Expect(world.GetBlock(glm::ivec3(3, 56, 3)) == cutum::BLOCK_AIR,
                    kTestName, "chunk prune removes floating canopy");
  FluidTest::Expect(world.GetBlock(glm::ivec3(3, 51, 3)) == kGrass, kTestName,
                    "chunk prune keeps coastal grass");
}

static void TestSpawnIslandMinSurface()
{
  cutum::ProceduralSettings settings;
  settings.FillWater = true;
  settings.SeaLevel = kSea;
  settings.MaxHeight = 128;
  const int adjusted = cutum::AdjustSurfaceYForSpawnIsland(0, 0, 46, settings);
  FluidTest::Expect(adjusted >= kSea + 3, kTestName,
                    "spawn island lifts surface to at least sea+3");
}

} // namespace

int main()
{
  TestResolvePlacementSurfaceOnCoast();
  TestRejectLeavesWithoutTrunk();
  TestAcceptGroundedTree();
  TestAcceptEmbeddedRootTree();
  TestAcceptBranchColumnTree();
  TestAcceptSurfaceLayerPath();
  TestAcceptFallenLog();
  TestResolveSurfaceLayerAnchorY();
  TestRejectMisTaggedVerticalPlantAsSurfaceLayer();
  TestPruneFloatingLeaves();
  TestSealAndPruneCoastalChunk();
  TestSpawnIslandMinSurface();

  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}
