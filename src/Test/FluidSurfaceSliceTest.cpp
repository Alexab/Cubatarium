#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "Render/Mesh/FluidSurfaceColumnSlice.h"
#include "World/Core/BlockWorld.h"
#include "World/Core/FluidColumnSurfaceQuery.h"
#include "World/Math/GridMath.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <unordered_map>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "fluid_surface_slice_test: " << message << std::endl;
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

static void FillFlatOcean(cutum::UBlockWorld &world, cutum::BlockId water_id,
                          int sea_y, int min_x, int max_x, int min_z,
                          int max_z)
{
  for (int x = min_x; x <= max_x; ++x)
  {
    for (int z = min_z; z <= max_z; ++z)
    {
      world.SetBlock(glm::ivec3(x, sea_y, z), water_id);
    }
  }
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

  FillFlatOcean(world, kWater, kSeaY, 0, 15, 0, 15);

  const cutum::FluidSurfaceColumnSlice slice =
      cutum::BuildFluidSurfaceColumnSlice(world, registry, glm::ivec3(0, 0, 0),
                                          kSeaY);
  for (int lz = 0; lz < cutum::CHUNK_SIZE; ++lz)
  {
    for (int lx = 0; lx < cutum::CHUNK_SIZE; ++lx)
    {
      Expect(slice.HasSurface(lx, lz), "flat ocean fills chunk slice");
      Expect(slice.SurfaceBlockY[lz][lx] == kSeaY, "ocean surface block y");
      Expect(slice.FluidId[lz][lx] == kWater, "ocean fluid id");
    }
  }

  const int corners[4][2] = {{0, 0}, {15, 0}, {0, 15}, {15, 15}};
  for (const auto &corner : corners)
  {
    const int bx = corner[0];
    const int bz = corner[1];
    const cutum::FluidColumnSurface direct =
        cutum::FindFluidColumnSurfaceAt(world, registry, bx, bz, kSeaY);
    Expect(direct.valid, "corner column valid");
    Expect(slice.SurfaceBlockY[bz][bx] == direct.surfaceBlockY,
           "slice matches direct query block y");
    Expect(slice.FluidId[bz][bx] == direct.fluidId,
           "slice matches direct query fluid id");
  }

  world.SetBlock(glm::ivec3(5, 10, 5), kLava);
  const cutum::FluidSurfaceColumnSlice lava_slice =
      cutum::BuildFluidSurfaceColumnSlice(world, registry, glm::ivec3(0, 0, 0),
                                          20);
  Expect(lava_slice.FluidId[5][5] == kLava, "lava pool column uses lava id");
  Expect(lava_slice.SurfaceBlockY[5][5] == 10, "lava pool surface block y");
  Expect(cutum::FluidSurfaceIndexForBlock(kLava, registry) == 2,
         "lava maps to shader index 2");
  Expect(cutum::FluidSurfaceIndexForBlock(kWater, registry) == 1,
         "water maps to shader index 1");

  std::cout << "fluid_surface_slice_test: ok" << std::endl;
  return 0;
}
