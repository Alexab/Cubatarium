#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "Render/Mesh/FluidSurfaceColumnSlice.h"
#include "Render/Mesh/GpuFluidColumnScan.h"
#include "World/Core/BlockWorld.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <unordered_map>

namespace
{

int gFails = 0;

void Expect(bool cond, const char *msg)
{
  if (!cond)
  {
    std::cerr << "FAIL: " << msg << "\n";
    ++gFails;
  }
}

std::shared_ptr<cutum::UBlockDefinitionStorage> MakeDefinitions()
{
  constexpr cutum::BlockId kWater = 9;
  constexpr cutum::BlockId kStone = 8;
  auto definitions = std::make_shared<cutum::UBlockDefinitionStorage>();
  cutum::BlockDefinition water;
  water.Name = "water";
  water.Id = kWater;
  water.Physics = cutum::BlockPhysicsProfile::FromPreset("water");
  water.Render.Transparent = true;
  water.Render.Style = cutum::BlockRenderStyle::Fluid;
  cutum::BlockDefinition stone;
  stone.Name = "stone";
  stone.Id = kStone;
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

} // namespace

int main()
{
  using namespace cutum;
  const auto definitions = MakeDefinitions();
  UBlockRegistry registry(nullptr, definitions);
  UBlockWorld world;
  world.SetFluidDefinitions(definitions.get());

  constexpr BlockId kWater = 9;
  constexpr int kSeaY = 63;
  const glm::ivec3 chunk_coord(2, 0, 3);

  SetPreferGpuFluidColumnScan(true);
  ResetFluidSurfacePackCacheHits();
  ResetFluidSurfacePackReuseCache();

  for (int x = 0; x < CHUNK_SIZE; ++x)
  {
    for (int z = 0; z < CHUNK_SIZE; ++z)
    {
      world.SetBlock(glm::ivec3(chunk_coord.x * CHUNK_SIZE + x, kSeaY,
                                chunk_coord.z * CHUNK_SIZE + z),
                     kWater);
    }
  }

  const FluidSurfaceColumnSlice first =
      BuildFluidSurfaceColumnSlice(world, registry, chunk_coord, kSeaY);
  Expect(FluidSurfacePackCacheHits() == 0, "first build no cache hit");
  Expect(first.HasSurface(0, 0), "first slice has surface");
  Expect(first.SurfaceBlockY[0][0] == kSeaY, "first surface y");

  const FluidSurfaceColumnSlice second =
      BuildFluidSurfaceColumnSlice(world, registry, chunk_coord, kSeaY);
  Expect(FluidSurfacePackCacheHits() == 1, "second build cache hit");
  Expect(second.SurfaceBlockY[0][0] == first.SurfaceBlockY[0][0],
         "cached slice matches");
  Expect(second.FluidId[0][0] == first.FluidId[0][0], "cached fluid id");

  // Changing fluid layout invalidates full-slice cache; tops may still reuse.
  world.SetBlock(glm::ivec3(chunk_coord.x * CHUNK_SIZE + 5, kSeaY,
                            chunk_coord.z * CHUNK_SIZE + 5),
                 BLOCK_AIR);
  const uint64_t hits_before = FluidSurfacePackCacheHits();
  const FluidSurfaceColumnSlice third =
      BuildFluidSurfaceColumnSlice(world, registry, chunk_coord, kSeaY);
  Expect(FluidSurfacePackCacheHits() == hits_before,
         "layout change skips full-slice hit");
  Expect(!third.HasSurface(5, 5), "removed column has no surface");

  SetPreferGpuFluidColumnScan(false);
  ResetFluidSurfacePackReuseCache();

  if (gFails != 0)
  {
    std::cerr << "fluid_surface_pack_reuse_test: " << gFails << " failures\n";
    return 1;
  }
  std::cout << "fluid_surface_pack_reuse_test: ok\n";
  return 0;
}
