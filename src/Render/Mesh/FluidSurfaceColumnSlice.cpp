#include "Render/Mesh/FluidSurfaceColumnSlice.h"

#include "Blocks/BlockRegistry.h"
#include "Render/Mesh/FluidColumnSummary.h"
#include "Render/Mesh/GpuFluidColumnScan.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/BlockWorld.h"
#include "World/Core/FluidColumnSurfaceQuery.h"
#include "World/Core/FluidSurfaceScanTuning.h"
#include "World/Math/GridMath.h"

#include <chrono>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace cutum
{

uint64_t gFluidPackCacheHits = 0;
uint64_t gFluidPackWorldEpoch = 1;

namespace
{

uint64_t SteadyNowMs()
{
  using clock = std::chrono::steady_clock;
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          clock::now().time_since_epoch())
          .count());
}

/// Incomplete tops-only entries older than this are rejected (A21 P6).
constexpr uint64_t kIncompleteTileMaxAgeMs = 2000;

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
  uint64_t content_rev{0};
  uint64_t catalog_rev{0};
  uint64_t fluid_id_hash{0};
  /// A21 P6: reject incomplete-tile reuse after world epoch bump / age.
  uint64_t world_epoch{0};
  uint64_t stored_steady_ms{0};
  bool incomplete{false};
  BlockId representative_fluid_id{BLOCK_AIR};
  std::vector<int16_t> tops;
  FluidSurfaceColumnSlice slice;
  bool has_slice{false};
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

uint64_t CatalogRevOf(const UBlockRegistry &registry)
{
  const auto catalog = registry.GetDefinitionsCatalogSnapshot();
  return catalog ? static_cast<uint64_t>(reinterpret_cast<uintptr_t>(catalog.get()))
                 : 0ull;
}

uint64_t ContentRevOf(const UBlockWorld &world, glm::ivec3 groundChunkCoord)
{
  if (const UChunk *chunk = world.GetChunkManager().GetChunk(groundChunkCoord))
  {
    return chunk->GetContentRevision();
  }
  return 0ull;
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
  const uint64_t content_rev = ContentRevOf(world, groundChunkCoord);
  const uint64_t catalog_rev = CatalogRevOf(registry);
  auto &cache = FluidPackReuseCache();
  // A22 S5: stamp hit before height×16×16 GetBlock (manual/AF fluid spikes).
  {
    const auto cit0 = cache.find(groundChunkCoord);
    if (cit0 != cache.end() && cit0->second.has_slice &&
        cit0->second.height == height && cit0->second.y_min == y_min &&
        cit0->second.content_rev == content_rev &&
        cit0->second.catalog_rev == catalog_rev &&
        cit0->second.world_epoch == gFluidPackWorldEpoch &&
        !cit0->second.incomplete)
    {
      slice = cit0->second.slice;
      ++gFluidPackCacheHits;
      return true;
    }
    if (cit0 != cache.end() && cit0->second.incomplete &&
        cit0->second.world_epoch == gFluidPackWorldEpoch &&
        cit0->second.height == height && cit0->second.y_min == y_min &&
        cit0->second.content_rev == content_rev &&
        cit0->second.catalog_rev == catalog_rev)
    {
      const uint64_t age = SteadyNowMs() - cit0->second.stored_steady_ms;
      if (age <= kIncompleteTileMaxAgeMs && cit0->second.has_slice)
      {
        slice = cit0->second.slice;
        ++gFluidPackCacheHits;
        return true;
      }
    }
  }
  std::vector<uint8_t> flags(static_cast<size_t>(height * n * n), 0);
  uint64_t scan_fluid_id_hash = 14695981039346656037ull;
  BlockId scan_representative = BLOCK_AIR;
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
          scan_fluid_id_hash ^= static_cast<uint64_t>(id);
          scan_fluid_id_hash *= 1099511628211ull;
          if (scan_representative == BLOCK_AIR)
          {
            scan_representative = id;
          }
        }
      }
    }
  }
  if (!any_fluid)
  {
    cache.erase(groundChunkCoord);
    return true; // empty slice already initialized by caller
  }

  const uint64_t pack_hash = HashFluidFlags(flags);
  const auto cit = cache.find(groundChunkCoord);
  // Full-slice hit requires occupancy + fluid identity + world/catalog stamps.
  // Occupancy-only match must not reuse water→lava FluidId payloads.
  // Incomplete tiles: reject after world epoch bump or age timeout.
  auto incomplete_ok = [&](const FluidPackCacheEntry &e) -> bool {
    if (!e.incomplete)
    {
      return true;
    }
    if (e.world_epoch != gFluidPackWorldEpoch)
    {
      return false;
    }
    const uint64_t age = SteadyNowMs() - e.stored_steady_ms;
    return age <= kIncompleteTileMaxAgeMs;
  };
  if (cit != cache.end() && cit->second.hash == pack_hash &&
      cit->second.height == height && cit->second.y_min == y_min &&
      cit->second.content_rev == content_rev &&
      cit->second.catalog_rev == catalog_rev &&
      cit->second.fluid_id_hash == scan_fluid_id_hash &&
      cit->second.representative_fluid_id == scan_representative &&
      cit->second.has_slice && incomplete_ok(cit->second))
  {
    slice = cit->second.slice;
    ++gFluidPackCacheHits;
    return true;
  }

  std::vector<int16_t> tops;
  if (cit != cache.end() && cit->second.hash == pack_hash &&
      cit->second.height == height && cit->second.y_min == y_min &&
      incomplete_ok(cit->second))
  {
    // Identical occupancy pack — reuse tops, skip scan. FluidId cells
    // are still re-read from the world below (identity may differ).
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
    entry.content_rev = content_rev;
    entry.catalog_rev = catalog_rev;
    entry.fluid_id_hash = scan_fluid_id_hash;
    entry.representative_fluid_id = scan_representative;
    entry.tops = tops;
    entry.world_epoch = gFluidPackWorldEpoch;
    entry.stored_steady_ms = SteadyNowMs();
    entry.incomplete = true; // tops only until full slice written below
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
  FluidPackCacheEntry &stored = cache[groundChunkCoord];
  stored.hash = pack_hash;
  stored.height = height;
  stored.y_min = y_min;
  stored.content_rev = content_rev;
  stored.catalog_rev = catalog_rev;
  // Same identity domain as the hit check (scan-time fluid cells), not the
  // 2D surface map — occupancy-matched water→lava must still miss.
  stored.fluid_id_hash = scan_fluid_id_hash;
  stored.representative_fluid_id = scan_representative;
  stored.tops = tops;
  stored.slice = slice;
  stored.has_slice = true;
  stored.incomplete = false;
  stored.world_epoch = gFluidPackWorldEpoch;
  stored.stored_steady_ms = SteadyNowMs();
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

uint64_t FluidSurfacePackCacheHits() { return gFluidPackCacheHits; }

void ResetFluidSurfacePackCacheHits() { gFluidPackCacheHits = 0; }

void ResetFluidSurfacePackReuseCache()
{
  FluidPackReuseCache().clear();
  // A21 P6: world switch / session reset invalidates incomplete-tile reuse.
  ++gFluidPackWorldEpoch;
}

} // namespace cutum
