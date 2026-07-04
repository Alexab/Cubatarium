#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"
#include "World/Core/FluidColumnSurfaceQuery.h"
#include "World/Math/GridMath.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <unordered_map>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "underwater_fog_column_test: " << message << std::endl;
    std::exit(1);
  }
}

static std::shared_ptr<cutum::UBlockDefinitionStorage> MakeDefinitions()
{
  constexpr cutum::BlockId kWater = 9;
  constexpr cutum::BlockId kLava = 11;
  constexpr cutum::BlockId kStone = 8;
  auto definitions = std::make_shared<cutum::UBlockDefinitionStorage>();
  cutum::BlockDefinition water;
  water.Name = "water";
  water.Id = kWater;
  water.Physics = cutum::BlockPhysicsProfile::FromPreset("water");
  water.Render.Transparent = true;
  water.Render.Style = cutum::BlockRenderStyle::Fluid;
  cutum::BlockDefinition lava;
  lava.Name = "lava";
  lava.Id = kLava;
  lava.Physics = cutum::BlockPhysicsProfile::FromPreset("lava");
  lava.Render.Transparent = true;
  lava.Render.Style = cutum::BlockRenderStyle::Fluid;
  cutum::BlockDefinition stone;
  stone.Name = "stone";
  stone.Id = kStone;
  stone.Physics = cutum::BlockPhysicsProfile::Solid();
  std::unordered_map<cutum::BlockId, cutum::BlockDefinition> by_id;
  by_id[kWater] = water;
  by_id[kLava] = lava;
  by_id[kStone] = stone;
  std::unordered_map<std::string, cutum::BlockId> name_to_id;
  name_to_id["water"] = kWater;
  name_to_id["lava"] = kLava;
  name_to_id["stone"] = kStone;
  definitions->ReplaceAll(std::move(by_id), std::move(name_to_id));
  return definitions;
}

static bool CameraInsideFluidRule(const cutum::FluidColumnSurface &column,
                                  float eyeY)
{
  return column.valid && eyeY < column.surfaceY;
}

int main()
{
  const auto definitions = MakeDefinitions();
  cutum::UBlockRegistry registry(nullptr, definitions);
  cutum::UBlockWorld world;
  world.SetFluidDefinitions(definitions.get());

  constexpr cutum::BlockId kWater = 9;
  constexpr cutum::BlockId kLava = 11;
  constexpr int kSeaY = 63;
  const int bx = 4;
  const int bz = 7;

  world.SetBlock(glm::ivec3(bx, kSeaY, bz), kWater);

  const cutum::FluidColumnSurface ocean =
      cutum::FindFluidColumnSurfaceAt(world, registry, bx, bz, kSeaY);
  Expect(ocean.valid, "water column has surface");
  Expect(ocean.fluidId == kWater, "top liquid is water");
  Expect(ocean.surfaceBlockY == kSeaY, "surface block y matches sea level");
  Expect(std::abs(ocean.surfaceY - cutum::BlockTopY(kSeaY)) < 1e-4f,
         "surfaceY is BlockTopY");

  const cutum::FluidColumnSurface empty =
      cutum::FindFluidColumnSurfaceAt(world, registry, 100, 100, 64);
  Expect(!empty.valid, "empty column has no surface");

  world.SetBlock(glm::ivec3(bx, kSeaY - 2, bz), kLava);
  const cutum::FluidColumnSurface stacked =
      cutum::FindFluidColumnSurfaceAt(world, registry, bx, bz, kSeaY);
  Expect(stacked.valid && stacked.fluidId == kWater,
         "water above lava wins for column surface");

  Expect(CameraInsideFluidRule(ocean, cutum::BlockTopY(kSeaY) - 0.1f),
         "eye below surface is inside fluid");
  Expect(!CameraInsideFluidRule(ocean, cutum::BlockTopY(kSeaY)),
         "eye at surface top is not inside fluid");
  Expect(!CameraInsideFluidRule(ocean, cutum::BlockTopY(kSeaY) + 0.5f),
         "eye above surface is not inside fluid");
  Expect(!CameraInsideFluidRule(empty, 32.0f),
         "empty column never counts as inside fluid");

  std::cout << "underwater_fog_column_test: ok" << std::endl;
  return 0;
}
