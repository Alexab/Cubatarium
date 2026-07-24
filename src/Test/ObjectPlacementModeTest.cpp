#include "Test/FluidTestHelpers.h"

#include "Blocks/BlockRegistry.h"
#include "World/Objects/ObjectLibrary.h"
#include "World/Objects/ObjectUtil.h"

#include <iostream>

namespace
{

constexpr const char *kTestName = "object_placement_mode_test";

static cutum::WorldObjectDefinition MakeSurfaceLayerObject()
{
  cutum::WorldObjectDefinition object;
  object.PlacementMode = cutum::ObjectPlacementMode::SurfaceLayer;
  object.anchor = glm::ivec3(0);
  object.voxels.push_back({glm::ivec3(0, 0, 0), 8});
  return object;
}

static void TestExplicitSurfaceLayerMode()
{
  cutum::UBlockRegistry registry(nullptr, FluidTest::MakeTestFluidDefinitions());
  const cutum::WorldObjectDefinition object = MakeSurfaceLayerObject();
  FluidTest::Expect(cutum::IsSurfaceLayerPrefab(object, registry), kTestName,
                    "explicit surface_layer mode");
  FluidTest::Expect(cutum::ResolveWorldGenAnchorY(object, registry, 51, 0) == 51,
                    kTestName, "surface layer anchor on top solid");
}

static void TestExplicitVerticalPlantMode()
{
  cutum::UBlockRegistry registry(nullptr, FluidTest::MakeTestFluidDefinitions());
  cutum::WorldObjectDefinition object;
  object.PlacementMode = cutum::ObjectPlacementMode::VerticalPlant;
  object.anchor = glm::ivec3(0);
  object.voxels.push_back({glm::ivec3(0, 0, 0), 8});
  object.voxels.push_back({glm::ivec3(0, 1, 0), 8});
  FluidTest::Expect(!cutum::IsSurfaceLayerPrefab(object, registry), kTestName,
                    "vertical_plant mode is not surface layer");
  FluidTest::Expect(cutum::ResolveWorldGenAnchorY(object, registry, 51, 0) == 52,
                    kTestName, "vertical plant anchor above top solid");
}

} // namespace

int main()
{
  TestExplicitSurfaceLayerMode();
  TestExplicitVerticalPlantMode();
  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}
