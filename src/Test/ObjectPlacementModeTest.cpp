#include "Test/FluidTestHelpers.h"

#include "Blocks/BlockRegistry.h"
#include "World/Objects/ObjectLibrary.h"
#include "World/Objects/ObjectUtil.h"

#include <filesystem>
#include <fstream>
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

static void TestTypedVoxelResolution()
{
  cutum::UBlockRegistry registry(nullptr, FluidTest::MakeTestFluidDefinitions());
  const cutum::ObjectVoxel stale{glm::ivec3(0), 9, "stone"};
  const cutum::ObjectVoxel unknown{glm::ivec3(0), 8, "missing:block"};
  const cutum::ObjectVoxel legacyValid{glm::ivec3(0), 8, ""};
  const cutum::ObjectVoxel legacyInvalid{glm::ivec3(0), 99, ""};
  FluidTest::Expect(cutum::ResolveObjectVoxelPlacementId(stale, registry) == 8,
                    kTestName, "typed voxel ignores stale numeric ID");
  FluidTest::Expect(cutum::ResolveObjectVoxelPlacementId(unknown, registry) ==
                        cutum::BLOCK_AIR,
                    kTestName, "unknown typed voxel resolves to AIR");
  FluidTest::Expect(cutum::ResolveObjectVoxelPlacementId(legacyValid, registry) ==
                        8,
                    kTestName, "registered legacy numeric ID remains valid");
  FluidTest::Expect(cutum::ResolveObjectVoxelPlacementId(legacyInvalid,
                                                        registry) ==
                        cutum::BLOCK_AIR,
                    kTestName, "unknown legacy numeric ID resolves to AIR");
}

static void TestRebindInvalidatesStaleId()
{
  const std::filesystem::path folder =
      std::filesystem::temp_directory_path() / "cubatarium_object_rebind_test";
  std::filesystem::create_directories(folder);
  const std::filesystem::path path = folder / "typed.json";
  {
    std::ofstream out(path);
    out << R"({"name":"typed","blocks":[{"dx":0,"dy":0,"dz":0,"type":"stone"}]})";
  }

  cutum::UBlockRegistry populated(nullptr, FluidTest::MakeTestFluidDefinitions());
  cutum::UObjectLibrary objects;
  objects.LoadMerged(folder, {}, populated);
  const auto object = objects.GetShared("typed");
  FluidTest::Expect(object && object->voxels.size() == 1 && object->voxels[0].Id == 8,
                    kTestName, "typed object loaded with active registry ID");

  cutum::UBlockRegistry empty(nullptr,
                              std::make_shared<cutum::UBlockDefinitionStorage>());
  objects.RebindBlockIds(empty);
  FluidTest::Expect(object->voxels[0].Id == cutum::BLOCK_AIR,
                    kTestName, "failed rebind invalidates stale pack ID");
  objects.RebindBlockIds(populated);
  FluidTest::Expect(object->voxels[0].Id == 8, kTestName,
                    "subsequent successful rebind restores current ID");
  std::filesystem::remove_all(folder);
}

} // namespace

int main()
{
  TestExplicitSurfaceLayerMode();
  TestExplicitVerticalPlantMode();
  TestTypedVoxelResolution();
  TestRebindInvalidatesStaleId();
  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}
