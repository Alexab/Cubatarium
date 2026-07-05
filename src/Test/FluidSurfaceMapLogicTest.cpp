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
}

static void TestUnderwaterFogPolicy()
{
  Expect(!cutum::ShouldUseGlobalUnderwaterFog(false, true),
         "above surface with map: no global underwater fog");
  Expect(cutum::ShouldUseGlobalUnderwaterFog(true, true),
         "submerged with map: global underwater fog");
  Expect(cutum::ShouldUseGlobalUnderwaterFog(true, false),
         "submerged without map: global fallback");
  Expect(cutum::ShouldUsePerColumnBelowSurfaceFog(true),
         "map ready enables per-column fog");
  Expect(!cutum::ShouldUsePerColumnBelowSurfaceFog(false),
         "map not ready disables per-column fog");
  Expect(std::abs(cutum::BelowSurfaceFogStrength(true, false) - 1.0f) < 1e-5f,
         "wading strength is 1");
  Expect(std::abs(cutum::BelowSurfaceFogStrength(true, true) - 0.25f) < 1e-5f,
         "submerged strength is a light supplement");
  Expect(std::abs(cutum::BelowSurfaceFogStrength(false, false)) < 1e-5f,
         "map not ready strength is 0");
  Expect(std::abs(cutum::BelowSurfaceFogDepthMin(false) - 0.5f) < 1e-5f,
         "wading skips shallow surface band");
  Expect(std::abs(cutum::BelowSurfaceFogDepthMin(true)) < 1e-5f,
         "submerged uses full below-surface column");

  cutum::FluidViewProfile profile;
  profile.FogMinBlend = 0.5f;
  profile.BelowSurfaceFogMin = 0.52f;
  Expect(std::abs(cutum::SubmergedBelowSurfaceFogMin(profile) - 0.52f) < 1e-5f,
         "submerged min uses higher of fog min blend and below-surface min");

  profile.FogMinBlend = 0.85f;
  Expect(std::abs(cutum::SubmergedBelowSurfaceFogMin(profile) - 0.85f) < 1e-5f,
         "submerged min prefers fog min blend when higher");
}

} // namespace

int main()
{
  TestSentinelAndTuningDefaults();
  TestWindowMoveThreshold();
  TestColumnScanFindsSurface();
  TestUnderwaterFogPolicy();
  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}
