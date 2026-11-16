#include "World/Lighting/GpuSkylightColumnSeed.h"
#include "Blocks/BlockRegistry.h"

namespace cutum
{
namespace
{
uint64_t gCpuSkylightApplies = 0;
}

// Compatibility diagnostics: there is currently no authoritative GPU sky
// dispatch or readback. CPU work must never count as successful GPU work.
uint64_t GpuSkylightSeedDispatchCount() { return 0; }
uint64_t ConsumeGpuSkylightSeedReadbackCount() { return 0; }
uint64_t ConsumeGpuSkylightSeedApplyCount() { return 0; }

uint64_t ConsumeCpuSkylightSeedApplyCount()
{
  const auto result = gCpuSkylightApplies;
  gCpuSkylightApplies = 0;
  return result;
}

bool TryGpuSeedSkylightColumns(const std::array<uint8_t, CHUNK_VOLUME> &,
                               std::array<uint8_t, CHUNK_VOLUME> &)
{
  return false;
}

bool ApplyCpuSkylightSeedToChunk(UChunk &chunk, UBlockRegistry &registry)
{
  std::array<uint8_t, CHUNK_VOLUME> occ{};
  for (int y = 0; y < CHUNK_SIZE; ++y)
  {
    for (int z = 0; z < CHUNK_SIZE; ++z)
    {
      for (int x = 0; x < CHUNK_SIZE; ++x)
      {
        const glm::ivec3 local(x, y, z);
        const BlockId id = chunk.GetBlockLocal(local);
        const int li = (y * CHUNK_SIZE + z) * CHUNK_SIZE + x;
        occ[static_cast<size_t>(li)] =
            (id != 0 && !registry.IsTransparent(id) && registry.IsSolid(id))
                ? 1u
                : (registry.IsLiquid(id) ? 2u : 0u);
      }
    }
  }
  std::array<uint8_t, CHUNK_VOLUME> sky{};
  // Preview only; the caller must still arrange full-context relighting.
  SeedSkylightColumnsCpu(occ, sky);
  for (int y = 0; y < CHUNK_SIZE; ++y)
  {
    for (int z = 0; z < CHUNK_SIZE; ++z)
    {
      for (int x = 0; x < CHUNK_SIZE; ++x)
      {
        const glm::ivec3 local(x, y, z);
        const int li = (y * CHUNK_SIZE + z) * CHUNK_SIZE + x;
        const int block_level = chunk.GetBlockLightLocal(local);
        chunk.SetLightLocal(local, static_cast<int>(sky[static_cast<size_t>(li)]),
                            block_level);
      }
    }
  }
  ++gCpuSkylightApplies;
  return true;
}

} // namespace cutum
