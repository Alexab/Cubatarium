#include "Render/Mesh/FluidSurfaceColumnSlice.h"

#include "Blocks/BlockRegistry.h"
#include "Render/Mesh/GpuFluidColumnScan.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/BlockWorld.h"
#include "World/Core/FluidColumnSurfaceQuery.h"
#include "World/Core/FluidSurfaceScanTuning.h"
#include "World/Math/GridMath.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace cutum
{
namespace
{

bool IsFluidSurfaceBlock(BlockId id, const UBlockRegistry &registry)
{
  return registry.IsLiquid(id) &&
         registry.GetRenderStyle(id) == BlockRenderStyle::Fluid;
}

struct FluidPackCacheEntry
{
  uint64_t hash{0};
  int height{0};
  int y_min{0};
  std::vector<int16_t> tops;
};

std::unordered_map<glm::ivec3, FluidPackCacheEntry, IVec3Hash> &
FluidPackReuseCache()
{
  static std::unordered_map<glm::ivec3, FluidPackCacheEntry, IVec3Hash> cache;
  return cache;
}

uint64_t HashFluidFlags(const std::vector<uint8_t> &flags)
{
  uint64_t h = 14695981039346656037ull;
  for (uint8_t b : flags)
  {
    h ^= static_cast<uint64_t>(b);
    h *= 1099511628211ull;
  }
  return h;
}

bool TryBuildSliceGpu(const UBlockWorld &world, UBlockRegistry &registry,
                      glm::ivec3 groundChunkCoord, int scanHintY,
                      FluidSurfaceColumnSlice &slice)
{
  if (!PreferGpuFluidColumnScan())
  {
    return false;
  }
  const int scan_up = FluidSurfaceScanTuning::ScanUp;
  const int scan_down = FluidSurfaceScanTuning::ScanDown;
  const int y_max = scanHintY + scan_up;
  const int y_min = scanHintY - scan_down;
  const int height = y_max - y_min + 1;
  if (height <= 0)
  {
    return false;
  }
  const int n = CHUNK_SIZE;
  const glm::ivec3 origin(groundChunkCoord.x * CHUNK_SIZE, 0,
                          groundChunkCoord.z * CHUNK_SIZE);
  std::vector<uint8_t> flags(static_cast<size_t>(height * n * n), 0);
  bool any_fluid = false;
  for (int ly = 0; ly < height; ++ly)
  {
    const int wy = y_min + ly;
    for (int lz = 0; lz < n; ++lz)
    {
      for (int lx = 0; lx < n; ++lx)
      {
        const BlockId id =
            world.GetBlock(glm::ivec3(origin.x + lx, wy, origin.z + lz));
        if (IsFluidSurfaceBlock(id, registry))
        {
          flags[static_cast<size_t>((ly * n + lz) * n + lx)] = 1;
          any_fluid = true;
        }
      }
    }
  }
  if (!any_fluid)
  {
    FluidPackReuseCache().erase(groundChunkCoord);
    return true; // empty slice already initialized by caller
  }

  const uint64_t pack_hash = HashFluidFlags(flags);
  auto &cache = FluidPackReuseCache();
  std::vector<int16_t> tops;
  const auto cit = cache.find(groundChunkCoord);
  if (cit != cache.end() && cit->second.hash == pack_hash &&
      cit->second.height == height && cit->second.y_min == y_min)
  {
    // P7: dirty remesh with identical packed flags — reuse tops, skip GPU.
    tops = cit->second.tops;
  }
  else
  {
    if (!TryGpuScanFluidColumns(flags.data(), height, tops))
    {
      return false;
    }
    FluidPackCacheEntry entry;
    entry.hash = pack_hash;
    entry.height = height;
    entry.y_min = y_min;
    entry.tops = tops;
    cache[groundChunkCoord] = std::move(entry);
  }

  for (int lz = 0; lz < n; ++lz)
  {
    for (int lx = 0; lx < n; ++lx)
    {
      const int16_t local_top = tops[static_cast<size_t>(lz * n + lx)];
      if (local_top < 0)
      {
        continue;
      }
      const int surface_y = y_min + local_top;
      const int bx = origin.x + lx;
      const int bz = origin.z + lz;
      const BlockId top_id = world.GetBlock(glm::ivec3(bx, surface_y, bz));
      int bottom_y = surface_y;
      // Walk packed flags instead of a second GetBlock column scan.
      for (int ly = local_top - 1; ly >= 0; --ly)
      {
        if (!flags[static_cast<size_t>((ly * n + lz) * n + lx)])
        {
          break;
        }
        bottom_y = y_min + ly;
      }
      slice.SurfaceBlockY[lz][lx] = static_cast<int16_t>(surface_y);
      slice.BottomBlockY[lz][lx] = static_cast<int16_t>(bottom_y);
      slice.FluidId[lz][lx] = top_id;
    }
  }
  return true;
}

} // namespace

uint8_t FluidSurfaceIndexForBlock(BlockId id, const UBlockRegistry &registry)
{
  if (id == BLOCK_AIR)
  {
    return 0;
  }
  const BlockId water_id = registry.GetIdByTypeName("water");
  if (water_id != BLOCK_AIR && id == water_id)
  {
    return 1;
  }
  const BlockId lava_id = registry.GetIdByTypeName("lava");
  if (lava_id != BLOCK_AIR && id == lava_id)
  {
    return 2;
  }
  if (registry.GetRenderStyle(id) == BlockRenderStyle::Fluid)
  {
    return 1;
  }
  return 0;
}

FluidSurfaceColumnSlice
BuildFluidSurfaceColumnSlice(const UBlockWorld &world, UBlockRegistry &registry,
                             glm::ivec3 groundChunkCoord, int scanHintY)
{
  FluidSurfaceColumnSlice slice;
  for (int lz = 0; lz < CHUNK_SIZE; ++lz)
  {
    for (int lx = 0; lx < CHUNK_SIZE; ++lx)
    {
      slice.SurfaceBlockY[lz][lx] = FluidSurfaceColumnSlice::kNoSurface;
      slice.BottomBlockY[lz][lx] = FluidSurfaceColumnSlice::kNoSurface;
      slice.FluidId[lz][lx] = BLOCK_AIR;
    }
  }

  if (TryBuildSliceGpu(world, registry, groundChunkCoord, scanHintY, slice))
  {
    return slice;
  }

  const glm::ivec3 origin(groundChunkCoord.x * CHUNK_SIZE, 0,
                          groundChunkCoord.z * CHUNK_SIZE);
  for (int lz = 0; lz < CHUNK_SIZE; ++lz)
  {
    for (int lx = 0; lx < CHUNK_SIZE; ++lx)
    {
      const int bx = origin.x + lx;
      const int bz = origin.z + lz;
      const FluidColumnSurface column =
          FindFluidColumnSurfaceAt(world, registry, bx, bz, scanHintY);
      if (!column.valid)
      {
        continue;
      }
      slice.SurfaceBlockY[lz][lx] = static_cast<int16_t>(column.surfaceBlockY);
      slice.BottomBlockY[lz][lx] = static_cast<int16_t>(column.bottomBlockY);
      slice.FluidId[lz][lx] = column.fluidId;
    }
  }
  return slice;
}

} // namespace cutum
