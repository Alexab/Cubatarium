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

int main()
{
  const auto definitions = MakeDefinitions();
  cutum::UBlockRegistry registry(nullptr, definitions);
  cutum::UBlockWorld world;
  world.SetFluidDefinitions(definitions.get());

  constexpr cutum::BlockId kWater = 9;
  constexpr cutum::BlockId kLava = 11;
  constexpr cutum::BlockId kStone = 8;
  constexpr int kSeaY = 63;
  const int bx = 4;
  const int bz = 7;

  world.SetBlock(glm::ivec3(bx, kSeaY, bz), kWater);

  const cutum::FluidColumnSurface ocean =
      cutum::FindFluidColumnSurfaceAt(world, registry, bx, bz, kSeaY);
  Expect(ocean.valid, "water column has surface");
  Expect(ocean.fluidId == kWater, "top liquid is water");
  Expect(ocean.surfaceBlockY == kSeaY, "surface block y matches sea level");
  Expect(ocean.bottomBlockY == kSeaY,
         "single-block water bottom matches surface");
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

  Expect(cutum::ContainsFluidAtHeight(ocean, cutum::BlockTopY(kSeaY) - 0.1f),
         "eye below surface is inside fluid");
  Expect(!cutum::ContainsFluidAtHeight(ocean, cutum::BlockTopY(kSeaY)),
         "eye at surface top is not inside fluid");
  Expect(!cutum::ContainsFluidAtHeight(ocean, cutum::BlockTopY(kSeaY) + 0.5f),
         "eye above surface is not inside fluid");
  Expect(!cutum::ContainsFluidAtHeight(empty, 32.0f),
         "empty column never counts as inside fluid");

  // Dry cave under an ocean: water above the rock ceiling must not classify
  // the eye as submerged, regardless of the surface-map scan window.
  constexpr int kCaveEyeY = 20;
  constexpr int kOceanFloorY = 60;
  for (int y = 22; y <= kOceanFloorY; ++y) // cave ceiling + floor mass
  {
    world.SetBlock(glm::ivec3(bx, y, bz), kStone);
  }
  for (int y = kOceanFloorY + 1; y <= kSeaY; ++y) // thin ocean above
  {
    world.SetBlock(glm::ivec3(bx, y, bz), kWater);
  }

  const cutum::FluidColumnSurface cave_default =
      cutum::FindFluidColumnSurfaceAt(world, registry, bx, bz, kCaveEyeY);
  Expect(!cave_default.valid,
         "default (slice-builder) window misses water 40 blocks up");

  const auto at = [&](float y)
  {
    return cutum::FindFluidColumnSurfaceForEye(world, registry,
                                               glm::vec3(bx, y, bz));
  };
  Expect(!at(kCaveEyeY).valid, "dry cave ignores ocean behind stone ceiling");
  Expect(!cutum::ContainsFluidAtHeight(ocean, kCaveEyeY),
         "even a distant surface sample excludes eyes below its bottom");

  for (int y = -48; y <= 21; ++y)
    world.SetBlock(glm::ivec3(bx, y, bz), kWater);
  const auto flooded = at(-40.0f);
  Expect(flooded.valid && flooded.fluidId == kWater &&
             flooded.bottomBlockY == -48 && flooded.surfaceBlockY == 21 &&
             cutum::ContainsFluidAtHeight(flooded, -40.0f),
         "negative-height flooded cave uses its own interval below rock");

  world.SetBlock(glm::ivec3(bx, -40, bz), cutum::BLOCK_AIR);
  world.SetBlock(glm::ivec3(bx, -39, bz), cutum::BLOCK_AIR);
  Expect(!at(-40.0f).valid, "air pocket inside flooded cave stays dry");
  const auto lower = at(-42.0f);
  Expect(lower.valid && lower.surfaceBlockY == -41,
         "air pocket separates liquid intervals");
  world.SetBlock(glm::ivec3(bx, -43, bz), kLava);
  const auto lava_eye = at(-43.0f);
  Expect(lava_eye.fluidId == kLava && lava_eye.surfaceBlockY == -43 &&
             lava_eye.bottomBlockY == -43,
         "adjacent liquid types retain their own fog material");
  const float top = cutum::BlockTopY(kSeaY);
  Expect(at(top + 0.1f).valid &&
             !cutum::ContainsFluidAtHeight(at(top + 0.1f), top + 0.1f),
         "just-emerged eye retains surface transition without submersion");
  Expect(!at(top + 0.3f).valid,
         "surface transition cannot reach through a distant air gap");
  Expect(cutum::ContainsFluidAtHeight(at(-48.5f), -48.5f) && !at(-48.51f).valid,
         "negative-coordinate bottom boundary matches block geometry");

  std::cout << "underwater_fog_column_test: ok" << std::endl;
  return 0;
}
