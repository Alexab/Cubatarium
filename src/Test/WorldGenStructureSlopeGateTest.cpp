#include "Test/FluidTestHelpers.h"

#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"
#include "World/Objects/ObjectUtil.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include "WorldGen/Core/WorldGenContext.h"
#include "WorldGen/Features/ObjectPlacementConstraints.h"

#include <iostream>
#include <memory>
#include <unordered_map>

namespace
{

constexpr const char *kTestName = "worldgen_structure_slope_gate_test";
constexpr cutum::BlockId kStone = 8;
constexpr cutum::BlockId kGrass = 22;
constexpr int kSea = 48;

static std::shared_ptr<cutum::UBlockDefinitionStorage> MakeDefinitions()
{
  auto definitions = std::make_shared<cutum::UBlockDefinitionStorage>();
  cutum::BlockDefinition stone;
  stone.Name = "stone";
  stone.Physics = cutum::BlockPhysicsProfile::Solid();
  cutum::BlockDefinition grass;
  grass.Name = "grass";
  grass.Physics = cutum::BlockPhysicsProfile::Solid();
  std::unordered_map<cutum::BlockId, cutum::BlockDefinition> by_id;
  by_id[kStone] = stone;
  by_id[kGrass] = grass;
  std::unordered_map<std::string, cutum::BlockId> name_to_id;
  name_to_id["stone"] = kStone;
  name_to_id["grass"] = kGrass;
  definitions->ReplaceAll(std::move(by_id), std::move(name_to_id));
  return definitions;
}

static void FillGrassSurfaceAt(cutum::UBlockWorld &world, int x, int z,
                               int surfaceY)
{
  for (int y = 0; y < surfaceY; ++y)
  {
    world.SetBlock(glm::ivec3(x, y, z), kStone);
  }
  world.SetBlock(glm::ivec3(x, surfaceY, z), kGrass);
}

static cutum::WorldGenContext MakeContext(cutum::UBlockWorld &world,
                                          cutum::UBlockRegistry &registry)
{
  cutum::ProceduralSettings settings;
  settings.SeaLevel = kSea;
  settings.MaxHeight = 128;
  cutum::WorldGenContext ctx(world, registry, settings);
  return ctx;
}

static void TestFlatAnchorHasLowGradient()
{
  cutum::UBlockWorld world;
  cutum::UBlockRegistry registry(nullptr, MakeDefinitions());
  for (int dx = -4; dx <= 4; dx += 4)
  {
    for (int dz = -4; dz <= 4; dz += 4)
    {
      FillGrassSurfaceAt(world, dx, dz, 51);
    }
  }
  const cutum::WorldGenContext ctx = MakeContext(world, registry);
  const float gradient =
      cutum::SampleStructureSurfaceGradient(ctx, 0, 0, 51);
  FluidTest::Expect(gradient <= 1.0f, kTestName,
                    "flat 3x3 anchor surface has low gradient");
}

static void TestSteepAnchorHasHighGradient()
{
  cutum::UBlockWorld world;
  cutum::UBlockRegistry registry(nullptr, MakeDefinitions());
  FillGrassSurfaceAt(world, 0, 0, 51);
  FillGrassSurfaceAt(world, 4, 0, 58);
  FillGrassSurfaceAt(world, -4, 0, 51);
  FillGrassSurfaceAt(world, 0, 4, 51);
  FillGrassSurfaceAt(world, 0, -4, 51);
  const cutum::WorldGenContext ctx = MakeContext(world, registry);
  const float gradient =
      cutum::SampleStructureSurfaceGradient(ctx, 0, 0, 51);
  FluidTest::Expect(gradient >= 6.0f, kTestName,
                    "steep neighbor raises structure gradient");
}

} // namespace

int main()
{
  TestFlatAnchorHasLowGradient();
  TestSteepAnchorHasHighGradient();
  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}
