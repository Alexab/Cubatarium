#include "World/Lighting/GpuLightingSeedBackend.h"
#include "World/Core/World.h"
#include "World/Chunks/Chunk.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Lighting/GpuSkylightColumnSeed.h"
#include "World/Math/GridMath.h"
#include "Blocks/BlockRegistry.h"
#include <chrono>

namespace cutum
{

GpuLightingSeedBackend::GpuLightingSeedBackend(UWorld &world, int relight_min,
                                               int relight_max)
    : World(world), RelightMin(relight_min), RelightMax(relight_max)
{
}

LightingSeedResult
GpuLightingSeedBackend::TrySeedColumnAtCommit(glm::ivec3 ground, double budget_ms)
{
  if (budget_ms <= 0.0)
  {
    return LightingSeedResult{false, 0.0};
  }
  // GetBlockRegistry assumes registry is live after world init (commit path).
  UBlockRegistry &registry = World.GetBlockRegistry();
  const auto t0 = std::chrono::high_resolution_clock::now();
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
  // applied only when every attempted slice in the Y-band was seeded within
  // budget; partial → PendingLight (R1 fail path).
  const bool applied = attempted > 0 && seeded == attempted;
  return LightingSeedResult{applied, elapsed};
}

} // namespace cutum
