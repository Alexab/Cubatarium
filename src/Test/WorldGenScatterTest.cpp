#include "Test/FluidTestHelpers.h"

#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"
#include "World/Objects/ObjectUtil.h"
#include "WorldGen/Core/WorldGenPlacementTuning.h"

#include <iostream>
#include <memory>
#include <unordered_map>

namespace
{

constexpr const char *kTestName = "worldgen_scatter_test";
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

static void FillLandColumn(cutum::UBlockWorld &world, int x, int z, int top_y)
{
  for (int y = 0; y < top_y; ++y)
  {
    world.SetBlock(glm::ivec3(x, y, z), kStone);
  }
  world.SetBlock(glm::ivec3(x, top_y, z), kGrass);
}

static void TestScatterSeaLevelGate()
{
  cutum::UBlockWorld world;
  cutum::UBlockRegistry registry(nullptr, MakeDefinitions());
  FillLandColumn(world, 0, 0, kSea);

  const int maxScanY = cutum::ComputeMaxScanY(kSea, kSea, 128);
  const int localSurface =
      cutum::FindTopSolidSurfaceY(world, registry, 0, 0, maxScanY);
  FluidTest::Expect(localSurface == kSea, kTestName,
                    "column top solid at sea level");
  FluidTest::Expect(
      localSurface <
          kSea + cutum::WorldGenPlacementTuning::MinLandAboveSea,
      kTestName, "scatter skips columns without land above sea");
}

static void TestScatterEligibleColumn()
{
  cutum::UBlockWorld world;
  cutum::UBlockRegistry registry(nullptr, MakeDefinitions());
  FillLandColumn(world, 0, 0, kSea + 2);

  const int maxScanY = cutum::ComputeMaxScanY(kSea + 2, kSea, 128);
  const int localSurface =
      cutum::FindTopSolidSurfaceY(world, registry, 0, 0, maxScanY);
  FluidTest::Expect(
      localSurface >= kSea + cutum::WorldGenPlacementTuning::MinLandAboveSea,
      kTestName, "column with grass above sea passes scatter gate");
  FluidTest::Expect(
      cutum::CanPlacePlantAt(world, registry, glm::ivec3(0, localSurface + 1, 0)),
      kTestName, "air above eligible surface accepts plant voxel");
}

} // namespace

int main()
{
  TestScatterSeaLevelGate();
  TestScatterEligibleColumn();
  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}
