#include "WorldGen/Core/IChunkPopulator.h"
#include "World/Core/BlockWorld.h"
#include "WorldGen/Core/IWorldGenPipeline.h"
#include "WorldGen/Core/Noise.h"
#include "WorldGen/Features/CaveCarver.h"
#include "World/Prefabs/Prefab.h"
#include <cstdint>
#include <memory>

namespace cutum
{

namespace
{

bool ChunkPassesCaveGate(int chunkWorldX, int chunkWorldZ, uint32_t seed,
                         const CaveParams &params)
{
  const float cx = static_cast<float>(chunkWorldX) + CHUNK_SIZE * 0.5f;
  const float cz = static_cast<float>(chunkWorldZ) + CHUNK_SIZE * 0.5f;
  const float gate = NormalizedFBM2D(cx * params.scale * 0.25f,
                                     cz * params.scale * 0.25f, seed + 3000, 2,
                                     0.5f, 2.0f);
  return gate > 0.42f;
}

uint32_t PipelineSettingsKey(const ProceduralSettings &settings)
{
  uint32_t key = settings.Seed;
  key ^= static_cast<uint32_t>(settings.Generator) * 2654435761u;
  key ^= static_cast<uint32_t>(settings.EnableCaves) << 1;
  key ^= static_cast<uint32_t>(settings.MaxHeight) << 2;
  return key;
}

struct ThreadLocalPipelineState
{
  uint32_t key{0};
  UBlockWorld world;
  std::unique_ptr<IWorldGenPipeline> pipeline;
};

ThreadLocalPipelineState &GetThreadLocalPipeline()
{
  thread_local ThreadLocalPipelineState state;
  return state;
}

IWorldGenPipeline *EnsureThreadLocalPipeline(UBlockRegistry &registry,
                                             UPrefabLibrary *prefabs,
                                             const std::string &ownerPackId,
                                             const ProceduralSettings &settings)
{
  ThreadLocalPipelineState &state = GetThreadLocalPipeline();
  const uint32_t key = PipelineSettingsKey(settings);
  if (!state.pipeline || state.key != key)
  {
    state.world.Clear();
    WorldGenContext ctx{state.world, registry, settings, prefabs};
    ctx.WorldgenOwnerPackId = ownerPackId;
    ctx.ResolveBlockIds();
    state.pipeline = UProceduralWorldGenFactory::Create(std::move(ctx));
    state.key = key;
  }
  else
  {
    state.world.Clear();
  }
  return state.pipeline.get();
}

} // namespace

PipelineChunkPopulator::PipelineChunkPopulator(UBlockRegistry &registry,
                                               UPrefabLibrary *prefabs,
                                               std::string worldgenOwnerPackId)
    : Registry(registry), Prefabs(prefabs),
      WorldgenOwnerPackId(std::move(worldgenOwnerPackId))
{
}

ChunkPopulateResult PipelineChunkPopulator::Populate(
    const ChunkPopulateRequest &request)
{
  ChunkPopulateResult result;
  result.coord = request.chunkCoord;
  result.token = request.token;

  ProceduralSettings settings = request.settings;
  if (settings.EnableCaves &&
      !ChunkPassesCaveGate(request.chunkCoord.x * CHUNK_SIZE,
                           request.chunkCoord.z * CHUNK_SIZE, settings.Seed,
                           settings.Caves))
  {
    settings.EnableCaves = false;
  }

  IWorldGenPipeline *pipeline =
      EnsureThreadLocalPipeline(Registry, Prefabs, WorldgenOwnerPackId, settings);
  if (!pipeline)
  {
    return result;
  }

  ThreadLocalPipelineState &tls = GetThreadLocalPipeline();
  UBlockWorld &genWorld = tls.world;

  const int baseX = request.chunkCoord.x * CHUNK_SIZE;
  const int baseZ = request.chunkCoord.z * CHUNK_SIZE;

  for (int lz = 0; lz < CHUNK_SIZE; ++lz)
  {
    for (int lx = 0; lx < CHUNK_SIZE; ++lx)
    {
      (void)pipeline->SurfaceYAt(baseX + lx, baseZ + lz);
      pipeline->GenerateColumn(baseX + lx, baseZ + lz);
    }
  }

  genWorld.ForEachBlock(
      [&](glm::ivec3 pos, BlockId id)
      { result.buffer.SetBlock(pos, id); });
  return result;
}

} // namespace cutum
