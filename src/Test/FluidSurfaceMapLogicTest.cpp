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

#include <vector>

#include <cstdint>

#include <chrono>



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

  Expect(cutum::URuntimeTuning::Get().FluidSurfaceWindowMoveThreshold == 32,
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

  Expect(!cutum::ShouldRefreshFluidSurfaceWindow(0, 0, 16, 0),

         "sub-threshold x shift does not refresh");

  Expect(cutum::ShouldRefreshFluidSurfaceWindow(0, 0, 32, 0),

         "x shift at threshold refreshes");

  Expect(cutum::ShouldRefreshFluidSurfaceWindow(0, 0, 0, 32),

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

         "dedicated cutout-only pass skips underwater fog");

  Expect(cutum::ShouldApplyBelowSurfaceFogToPass(false, false),

         "merged opaque (alpha-discard mode) still gets underwater fog");

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


static void TestChunkStagingParityWithPerCell()
{
  constexpr cutum::BlockId kWater = 9;
  cutum::UBlockWorld world;
  const auto definitions = MakeDefinitions();
  cutum::UBlockRegistry registry(nullptr, definitions);
  for (int z = 0; z < 16; ++z)
  {
    for (int x = 0; x < 16; ++x)
    {
      world.SetBlock(glm::ivec3(x, 40, z), kWater);
    }
  }

  const int sizeBlocks = 16;
  const glm::ivec2 origin(0, 0);
  const size_t n = static_cast<size_t>(sizeBlocks) * sizeBlocks;
  const float sentinel = cutum::FluidSurfaceStagingSentinel();

  std::vector<float> chunkSurface(n, sentinel);
  std::vector<uint8_t> chunkIndex(n, 0);
  std::vector<float> chunkBottom(n, sentinel);
  const cutum::FluidSurfaceColumnSlice slice =
      cutum::BuildFluidSurfaceColumnSlice(world, registry, glm::ivec3(0, 0, 0),
                                          45);
  cutum::PatchFluidSurfaceStagingChunk(chunkSurface, chunkIndex, chunkBottom,
                                       sizeBlocks, origin, glm::ivec3(0, 0, 0),
                                       &slice, registry, sentinel);

  std::vector<float> cellSurface(n, sentinel);
  std::vector<uint8_t> cellIndex(n, 0);
  std::vector<float> cellBottom(n, sentinel);
  for (int dz = 0; dz < sizeBlocks; ++dz)
  {
    for (int dx = 0; dx < sizeBlocks; ++dx)
    {
      const cutum::FluidColumnSurface column =
          cutum::FindFluidColumnSurfaceAt(world, registry, dx, dz, 45);
      const size_t i = static_cast<size_t>(dz) * sizeBlocks + dx;
      if (!column.valid)
      {
        continue;
      }
      cellSurface[i] = cutum::BlockTopY(column.surfaceBlockY);
      cellBottom[i] = static_cast<float>(column.bottomBlockY);
      cellIndex[i] =
          cutum::FluidSurfaceIndexForBlock(column.fluidId, registry);
    }
  }

  for (size_t i = 0; i < n; ++i)
  {
    Expect(std::abs(chunkSurface[i] - cellSurface[i]) < 0.001f,
           "chunk staging surface matches per-cell");
    Expect(chunkIndex[i] == cellIndex[i],
           "chunk staging fluid index matches per-cell");
    Expect(std::abs(chunkBottom[i] - cellBottom[i]) < 0.001f,
           "chunk staging bottom matches per-cell");
  }
}

static void TestChunkWindowRebuildBudget()
{
  // Steady-state win is "no rebuild" when FluidSurfaceDirty is empty; this
  // checks that a single window fill via chunk iteration matches per-cell
  // reference data (RD=2).
  constexpr cutum::BlockId kWater = 9;
  constexpr int kRenderDist = 2;
  cutum::UBlockWorld world;
  const auto definitions = MakeDefinitions();
  cutum::UBlockRegistry registry(nullptr, definitions);
  for (int gz = -kRenderDist; gz <= kRenderDist; ++gz)
  {
    for (int gx = -kRenderDist; gx <= kRenderDist; ++gx)
    {
      for (int z = 0; z < 16; ++z)
      {
        for (int x = 0; x < 16; ++x)
        {
          world.SetBlock(glm::ivec3(gx * 16 + x, 40, gz * 16 + z), kWater);
        }
      }
    }
  }

  const int sizeBlocks = (2 * kRenderDist + 1) * 16;
  const glm::ivec2 origin(-kRenderDist * 16, -kRenderDist * 16);
  const size_t n = static_cast<size_t>(sizeBlocks) * sizeBlocks;
  const float sentinel = cutum::FluidSurfaceStagingSentinel();

  std::vector<float> cellSurface(n, sentinel);
  std::vector<uint8_t> cellIndex(n, 0);
  std::vector<float> cellBottom(n, sentinel);
  for (int dz = 0; dz < sizeBlocks; ++dz)
  {
    for (int dx = 0; dx < sizeBlocks; ++dx)
    {
      const int bx = origin.x + dx;
      const int bz = origin.y + dz;
      const cutum::FluidColumnSurface column =
          cutum::FindFluidColumnSurfaceAt(world, registry, bx, bz, 45);
      const size_t i = static_cast<size_t>(dz) * sizeBlocks + dx;
      if (!column.valid)
      {
        continue;
      }
      cellSurface[i] = cutum::BlockTopY(column.surfaceBlockY);
      cellBottom[i] = static_cast<float>(column.bottomBlockY);
      cellIndex[i] =
          cutum::FluidSurfaceIndexForBlock(column.fluidId, registry);
    }
  }

  std::vector<float> chunkSurface(n, sentinel);
  std::vector<uint8_t> chunkIndex(n, 0);
  std::vector<float> chunkBottom(n, sentinel);
  const auto t0 = std::chrono::high_resolution_clock::now();
  for (int gz = -kRenderDist; gz <= kRenderDist; ++gz)
  {
    for (int gx = -kRenderDist; gx <= kRenderDist; ++gx)
    {
      const glm::ivec3 ground(gx, 0, gz);
      const cutum::FluidSurfaceColumnSlice slice =
          cutum::BuildFluidSurfaceColumnSlice(world, registry, ground, 45);
      cutum::PatchFluidSurfaceStagingChunk(chunkSurface, chunkIndex,
                                           chunkBottom, sizeBlocks, origin,
                                           ground, &slice, registry, sentinel);
    }
  }
  const double chunk_ms = std::chrono::duration<double, std::milli>(
                              std::chrono::high_resolution_clock::now() - t0)
                              .count();
  std::cout << kTestName << ": window chunk-path ms=" << chunk_ms << std::endl;

  for (size_t i = 0; i < n; ++i)
  {
    Expect(std::abs(chunkSurface[i] - cellSurface[i]) < 0.001f,
           "window chunk staging surface matches per-cell");
    Expect(chunkIndex[i] == cellIndex[i],
           "window chunk staging index matches per-cell");
    Expect(std::abs(chunkBottom[i] - cellBottom[i]) < 0.001f,
           "window chunk staging bottom matches per-cell");
  }
}

static void TestScrollStagingPreservesOverlap()
{
  constexpr int kSize = 48;
  const size_t n = static_cast<size_t>(kSize) * kSize;
  const float sentinel = cutum::FluidSurfaceStagingSentinel();
  std::vector<float> surface(n, sentinel);
  std::vector<uint8_t> index(n, 0);
  std::vector<float> bottom(n, sentinel);
  // Mark a known texel at local (20, 10).
  const size_t marked =
      static_cast<size_t>(10) * kSize + static_cast<size_t>(20);
  surface[marked] = 42.5f;
  index[marked] = 3;
  bottom[marked] = 12.0f;

  const glm::ivec2 oldOrigin(0, 0);
  const glm::ivec2 newOrigin(16, 0);
  Expect(cutum::ScrollFluidSurfaceStagingWindow(surface, index, bottom, kSize,
                                                oldOrigin, newOrigin, sentinel),
         "scroll returns true on origin shift");

  // World block (20, 10) was at staging (20,10); after origin +=16x it is at (4,10).
  const size_t scrolled =
      static_cast<size_t>(10) * kSize + static_cast<size_t>(4);
  Expect(std::abs(surface[scrolled] - 42.5f) < 0.001f,
         "scrolled surface preserves overlap texel");
  Expect(index[scrolled] == 3, "scrolled index preserves overlap texel");
  Expect(std::abs(bottom[scrolled] - 12.0f) < 0.001f,
         "scrolled bottom preserves overlap texel");
  Expect(surface[marked] == sentinel || std::abs(surface[marked] - 42.5f) > 0.1f,
         "old edge slot vacated or rewritten");
  Expect(cutum::FluidSurfaceChunkNeedsStripPatch(glm::ivec3(3, 0, 0), oldOrigin,
                                                 newOrigin, kSize),
         "new right strip chunk needs patch");
  Expect(!cutum::FluidSurfaceChunkNeedsStripPatch(glm::ivec3(1, 0, 0), oldOrigin,
                                                  newOrigin, kSize),
         "fully interior old chunk does not need strip patch");
}

} // namespace

int main()
{
  TestSentinelAndTuningDefaults();
  TestWindowMoveThreshold();
  TestColumnScanFindsSurface();
  TestUnderwaterFogPolicyV3();
  TestChunkStagingParityWithPerCell();
  TestChunkWindowRebuildBudget();
  TestScrollStagingPreservesOverlap();
  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}
