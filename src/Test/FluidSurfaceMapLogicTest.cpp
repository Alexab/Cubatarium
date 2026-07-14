#include "Render/Engine/FluidSurfaceMapLogic.h"

#include "Render/Engine/FluidUnderwaterFogLogic.h"

#include "Render/Mesh/FluidSurfaceColumnSlice.h"

#include "World/Core/FluidColumnSurfaceQuery.h"

#include "World/Core/RuntimeTuning.h"



#include "Blocks/BlockDefinitionStorage.h"

#include "Blocks/BlockRegistry.h"

#include "World/Core/BlockWorld.h"



#include <cmath>

#include <cstdlib>

#include <iostream>

#include <memory>

#include <unordered_map>



namespace

{



constexpr const char *kTestName = "fluid_surface_map_logic_test";



static void Expect(bool cond, const char *message)

{

  if (!cond)

  {

    std::cerr << kTestName << ": " << message << std::endl;

    std::exit(1);

  }

}



static std::shared_ptr<cutum::UBlockDefinitionStorage> MakeDefinitions()

{

  constexpr cutum::BlockId kWater = 9;

  constexpr cutum::BlockId kStone = 8;

  auto definitions = std::make_shared<cutum::UBlockDefinitionStorage>();

  cutum::BlockDefinition water;

  water.Name = "water";

  water.Physics = cutum::BlockPhysicsProfile::FromPreset("water");

  water.Render.Transparent = true;

  water.Render.Style = cutum::BlockRenderStyle::Fluid;

  cutum::BlockDefinition stone;

  stone.Name = "stone";

  stone.Physics = cutum::BlockPhysicsProfile::Solid();

  std::unordered_map<cutum::BlockId, cutum::BlockDefinition> by_id;

  by_id[kWater] = water;

  by_id[kStone] = stone;

  std::unordered_map<std::string, cutum::BlockId> name_to_id;

  name_to_id["water"] = kWater;

  name_to_id["stone"] = kStone;

  definitions->ReplaceAll(std::move(by_id), std::move(name_to_id));

  return definitions;

}



static void TestSentinelAndTuningDefaults()

{

  Expect(std::abs(cutum::FluidSurfaceStagingSentinel() + 1000.0f) < 0.001f,

         "staging sentinel is -1000");

  cutum::URuntimeTuning::ResetToDefaults();

  Expect(cutum::URuntimeTuning::Get().FluidSurfaceWindowMoveThreshold == 8,

         "window move threshold default");

  Expect(cutum::URuntimeTuning::Get().FluidSurfaceScanUp == 32, "scan up default");

  Expect(cutum::URuntimeTuning::Get().FluidSurfaceScanDown == 64,

         "scan down default");

  Expect(cutum::FluidSurfaceColumnSlice::kNoSurface == INT16_MIN,

         "column slice no-surface sentinel");

}



static void TestWindowMoveThreshold()

{

  Expect(!cutum::ShouldRefreshFluidSurfaceWindow(0, 0, 4, 4),

         "small window shift does not refresh");

  Expect(cutum::ShouldRefreshFluidSurfaceWindow(0, 0, 8, 0),

         "x shift at threshold refreshes");

  Expect(cutum::ShouldRefreshFluidSurfaceWindow(0, 0, 0, 8),

         "z shift at threshold refreshes");

}



static void TestColumnScanFindsSurface()

{

  constexpr cutum::BlockId kWater = 9;

  cutum::UBlockWorld world;

  const auto definitions = MakeDefinitions();

  cutum::UBlockRegistry registry(nullptr, definitions);

  world.SetBlock(glm::ivec3(0, 40, 0), kWater);

  world.SetBlock(glm::ivec3(0, 50, 0), kWater);



  const cutum::FluidColumnSurface high =

      cutum::FindFluidColumnSurfaceAt(world, registry, 0, 0, 45);

  Expect(high.valid && high.surfaceBlockY == 50,

         "scan finds topmost fluid block");



  const cutum::FluidColumnSurface below_hint =

      cutum::FindFluidColumnSurfaceAt(world, registry, 0, 0, 10, 8, 8);

  Expect(!below_hint.valid, "narrow scan misses fluid outside range");



  Expect(cutum::HasFluidSurfaceNear(world, registry, 0, 0, 45, 16),

         "proximity finds nearby fluid column");

  Expect(!cutum::HasFluidSurfaceNear(world, registry, 64, 64, 45, 16),

         "proximity misses distant fluid column");

}



static void TestUnderwaterFogPolicyV3()

{

  Expect(!cutum::ShouldUseGlobalUnderwaterFog(false, true),

         "above surface with map: no global underwater fog");

  Expect(!cutum::ShouldUseGlobalUnderwaterFog(true, true),

         "submerged with map: per-column fog only");

  Expect(cutum::ShouldUseGlobalUnderwaterFog(true, false),

         "submerged without map: global fallback");

  Expect(cutum::ShouldUsePerColumnUnderwaterFog(true, true),

         "map ready near fluids enables per-column fog");

  Expect(!cutum::ShouldUsePerColumnUnderwaterFog(true, false),

         "map ready far from fluids disables per-column fog");

  Expect(!cutum::ShouldUsePerColumnUnderwaterFog(false, true),

         "map not ready disables per-column fog");

  Expect(cutum::IsShallowFluidSpan(12, 12), "single-block fluid is shallow");

  Expect(cutum::IsShallowFluidSpan(13, 12), "two-block fluid is shallow");

  Expect(!cutum::IsShallowFluidSpan(20, 12), "deep column is not shallow");

  Expect(cutum::IsPartialSubmerge(10.2f, 10.4f),

         "eye near surface is partial submerge");

  Expect(!cutum::IsPartialSubmerge(11.0f, 10.4f),

         "eye far above surface is not partial submerge");

  Expect(cutum::ShouldApplyUnderwaterFogToColumn(true, false, 20, 12),

         "submerged camera always uses column fog");

  Expect(cutum::ShouldApplyUnderwaterFogToColumn(false, true, 12, 12),

         "partial submerge uses column fog");

  Expect(!cutum::ShouldApplyUnderwaterFogToColumn(false, false, 12, 12),

         "shallow puddle on land skips column fog");

  Expect(cutum::ShouldApplyUnderwaterFogToColumn(false, false, 20, 12),
         "deep ocean from shore uses column fog");
  Expect(cutum::ShouldApplyBelowSurfaceFogToPass(false),

         "opaque pass receives underwater fog");

  Expect(!cutum::ShouldApplyBelowSurfaceFogToPass(true),

         "transparent fluid pass skips underwater fog");

  Expect(!cutum::ShouldApplyBelowSurfaceFogToPass(false, true),

         "cutout pass skips underwater fog");

  Expect(cutum::ShouldTintBlockBelowFluidColumn(9, 10, 10),

         "pool floor block is directly under fluid span");

  Expect(cutum::ShouldTintBlockBelowFluidColumn(63, 63, 63),
         "shore block at surface level can receive underwater fog");

  Expect(!cutum::ShouldTintBlockBelowFluidColumn(9, 15, 15),

         "ground far below isolated canopy water is not tinted");

  Expect(!cutum::ShouldTintBlockBelowFluidColumn(11, 15, 15),

         "dry block in gap below disjoint surface water is not tinted");

  Expect(cutum::ShouldTintBlockBelowFluidColumn(9, 10, 15),

         "ground below contiguous fluid column is tinted");

  Expect(cutum::ShouldTintBlockBelowFluidColumn(4, 5, 20),
         "deep ocean floor is directly under fluid span");
}



} // namespace



int main()

{

  TestSentinelAndTuningDefaults();

  TestWindowMoveThreshold();

  TestColumnScanFindsSurface();

  TestUnderwaterFogPolicyV3();

  std::cout << kTestName << ": OK" << std::endl;

  return 0;

}

