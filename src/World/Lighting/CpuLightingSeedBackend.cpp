#include "World/Lighting/CpuLightingSeedBackend.h"
#include "World/Core/World.h"
#include "World/Chunks/Chunk.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Lighting/GpuSkylightColumnSeed.h"
#include "World/Math/GridMath.h"
#include "Blocks/BlockRegistry.h"
#include <chrono>

namespace cutum
{

CpuLightingSeedBackend::CpuLightingSeedBackend(UWorld &world, int relight_min,
                                               int relight_max)
    : World(world), RelightMin(relight_min), RelightMax(relight_max)
{
}

LightingSeedResult
CpuLightingSeedBackend::TrySeedColumnAtCommit(glm::ivec3 ground, double budget_ms)
{
  if (budget_ms <= 0.0)
  {
    return LightingSeedResult{false, 0.0};
  }
  const auto t0 = std::chrono::high_resolution_clock::now();

  // Cheap path (cruise / tight budget): column skylight seed only — same algo
  // as GpuLightingSeedBackend. Full RelightTerrainColumn is reserved for idle
  // underfeet budgets (>2.5ms) to avoid 1–7s commit hitches.
  if (budget_ms <= 2.5)
  {
    UBlockRegistry &registry = World.GetBlockRegistry();
    const int cy0 = FloorDiv(RelightMin, CHUNK_SIZE);
    const int cy1 = FloorDiv(RelightMax, CHUNK_SIZE);
    int seeded = 0;
    int attempted = 0;
    for (int cy = cy0; cy <= cy1; ++cy)
    {
      const double elapsed_so_far =
          std::chrono::duration<double, std::milli>(
              std::chrono::high_resolution_clock::now() - t0)
              .count();
      if (elapsed_so_far > budget_ms)
      {
        break;
      }
      UChunk *chunk = World.GetBlockWorld().GetChunkManager().GetChunk(
          glm::ivec3(ground.x, cy, ground.z));
      if (!chunk)
      {
        continue;
      }
      ++attempted;
      if (ApplyGpuSkylightSeedToChunk(*chunk, registry))
      {
        ++seeded;
      }
    }
    const double elapsed =
        std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - t0)
            .count();
    return LightingSeedResult{attempted > 0 && seeded == attempted, elapsed};
  }

  World.RelightTerrainColumn(ground.x * CHUNK_SIZE, ground.z * CHUNK_SIZE,
                             RelightMin, RelightMax,
                             /*priority_mesh=*/true,
                             /*include_skylight=*/true,
                             /*include_block_light=*/true);
  const double elapsed =
      std::chrono::duration<double, std::milli>(
          std::chrono::high_resolution_clock::now() - t0)
          .count();
  return LightingSeedResult{true, elapsed};
}

} // namespace cutum
