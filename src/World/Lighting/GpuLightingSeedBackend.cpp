#include "World/Lighting/GpuLightingSeedBackend.h"
#include "World/Core/World.h"
#include "World/Chunks/Chunk.h"
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
  const auto t0 = std::chrono::high_resolution_clock::now();
  // GPU full-light path is selected internally by world/lighting pipeline.
  // Real Gpu vs Cpu split lands in R4; stub still writes via RelightTerrainColumn.
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
