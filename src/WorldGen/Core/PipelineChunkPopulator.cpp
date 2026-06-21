#include "WorldGen/Core/IChunkPopulator.h"
#include "World/Core/BlockWorld.h"
#include "WorldGen/Core/IWorldGenPipeline.h"
#include "WorldGen/Core/Noise.h"
#include "WorldGen/Features/CaveCarver.h"
#include "World/Prefabs/Prefab.h"

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

  UBlockWorld tempWorld;
  WorldGenContext ctx{tempWorld, Registry, settings, Prefabs};
  ctx.WorldgenOwnerPackId = WorldgenOwnerPackId;
  ctx.ResolveBlockIds();

  auto pipeline = UProceduralWorldGenFactory::Create(std::move(ctx));
  if (!pipeline)
  {
    return result;
  }

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

  tempWorld.ForEachBlock(
      [&](glm::ivec3 pos, BlockId id)
      { result.buffer.SetBlock(pos, id); });
  return result;
}

} // namespace cutum
