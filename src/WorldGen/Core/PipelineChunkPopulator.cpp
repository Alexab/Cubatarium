#include "World/Core/BlockWorld.h"
#include "World/Math/FluidCellState.h"
#include "World/Objects/ObjectLibrary.h"
#include "WorldGen/Core/BlockWorldColumnWriter.h"
#include "WorldGen/Core/IUChunkPopulator.h"
#include "WorldGen/Core/IUWorldGenPipeline.h"
#include "WorldGen/Core/Noise.h"
#include "WorldGen/Features/CaveCarver.h"
#include "WorldGen/Features/ObjectFeaturePlacer.h"
#include "WorldGen/Pipelines/ColumnGenerationService.h"
#include "WorldGen/Pipelines/ComposableWorldGenerator.h"
#include "WorldGen/Stages/WorldGenStages.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>

namespace cutum
{

namespace
{

bool ChunkPassesCaveGate(int chunkWorldX, int chunkWorldZ, uint32_t seed,
                         const CaveParams &params)
{
  if (!params.enableChunkGate)
  {
    return true;
  }
  const float cx = static_cast<float>(chunkWorldX) + CHUNK_SIZE * 0.5f;
  const float cz = static_cast<float>(chunkWorldZ) + CHUNK_SIZE * 0.5f;
  const float gate =
      NormalizedFBM2D(cx * params.scale * 0.25f, cz * params.scale * 0.25f,
                      seed + 3000, 2, 0.5f, 2.0f);
  return gate > params.chunkGateThreshold;
}

uint32_t PipelineSettingsKey(const ProceduralSettings &settings,
                             UObjectLibrary *prefabs)
{
  uint32_t key = settings.Seed;
  key ^= static_cast<uint32_t>(settings.Generator) * 2654435761u;
  key ^= static_cast<uint32_t>(settings.EnableCaves) << 1;
  key ^= static_cast<uint32_t>(settings.MaxHeight) << 2;
  key ^= static_cast<uint32_t>(settings.EnableTrees) << 3;
  key ^= static_cast<uint32_t>(settings.EnableGroundCover) << 4;
  key ^= static_cast<uint32_t>(settings.EnableDecoration) << 5;
  key ^= static_cast<uint32_t>(reinterpret_cast<uintptr_t>(prefabs) >> 4);
  return key;
}

void RefreshThreadLocalPipelineContext(UBlockRegistry &registry,
                                       UObjectLibrary *prefabs,
                                       const std::string &ownerPackId,
                                       const ProceduralSettings &settings,
                                       IUWorldGenPipeline *pipeline)
{
  if (!pipeline)
  {
    return;
  }
  auto *composable = dynamic_cast<UComposableWorldGenerator *>(pipeline);
  if (!composable)
  {
    return;
  }
  WorldGenContext &ctx = composable->GetContext();
  ctx.Objects = prefabs;
  ctx.Settings = settings;
  ctx.WorldgenOwnerPackId = ownerPackId;
  ctx.ResolveBlockIds();
  if (prefabs)
  {
    prefabs->RebindBlockIds(registry);
  }
}

struct ThreadLocalPipelineState
{
  uint32_t key{0};
  UBlockWorld world;
  std::unique_ptr<IUWorldGenPipeline> pipeline;
};

ThreadLocalPipelineState &GetThreadLocalPipeline()
{
  thread_local ThreadLocalPipelineState state;
  return state;
}

IUWorldGenPipeline *
EnsureThreadLocalPipeline(UBlockRegistry &registry, UObjectLibrary *prefabs,
                          const std::string &ownerPackId,
                          const ProceduralSettings &settings)
{
  ThreadLocalPipelineState &state = GetThreadLocalPipeline();
  const uint32_t key = PipelineSettingsKey(settings, prefabs);
  if (!state.pipeline || state.key != key)
  {
    state.world.Clear();
    WorldGenContext ctx{state.world, registry, settings, prefabs};
    ctx.WorldgenOwnerPackId = ownerPackId;
    ctx.ResolveBlockIds();
    if (prefabs)
    {
      prefabs->RebindBlockIds(registry);
    }
    state.pipeline = UProceduralWorldGenFactory::Create(std::move(ctx));
    state.key = key;
  }
  else
  {
    state.world.Clear();
    RefreshThreadLocalPipelineContext(registry, prefabs, ownerPackId, settings,
                                    state.pipeline.get());
  }
  return state.pipeline.get();
}

} // namespace

UPipelineChunkPopulator::UPipelineChunkPopulator(
    UBlockRegistry &registry, UObjectLibrary *prefabs,
    std::string worldgenOwnerPackId)
    : Registry(registry), Objects(prefabs),
      WorldgenOwnerPackId(std::move(worldgenOwnerPackId))
{
}

ChunkPopulateResult
UPipelineChunkPopulator::Populate(const ChunkPopulateRequest &request)
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

  UObjectLibrary *objects = request.objects ? request.objects : Objects;
  IUWorldGenPipeline *pipeline = EnsureThreadLocalPipeline(
      Registry, objects, WorldgenOwnerPackId, settings);
  if (!pipeline)
  {
    return result;
  }

  ThreadLocalPipelineState &tls = GetThreadLocalPipeline();
  UBlockWorld &genWorld = tls.world;
  ResetScatterChunkCounts();

  const int base_x = request.chunkCoord.x * CHUNK_SIZE;
  const int base_z = request.chunkCoord.z * CHUNK_SIZE;
  const int center_x = base_x + CHUNK_SIZE / 2;
  const int center_z = base_z + CHUNK_SIZE / 2;
  const int origin_x =
      request.hasColumnOrigin ? request.columnOrigin.x : center_x;
  const int origin_z =
      request.hasColumnOrigin ? request.columnOrigin.y : center_z;

  std::array<std::pair<int, int>, CHUNK_SIZE * CHUNK_SIZE> columns;
  size_t column_count = 0;
  for (int lz = 0; lz < CHUNK_SIZE; ++lz)
  {
    for (int lx = 0; lx < CHUNK_SIZE; ++lx)
    {
      columns[column_count++] = {lx, lz};
    }
  }
  std::sort(columns.begin(),
            columns.begin() + static_cast<ptrdiff_t>(column_count),
            [base_x, base_z, origin_x, origin_z](const std::pair<int, int> &a,
                                                 const std::pair<int, int> &b)
            {
              const int ax = base_x + a.first;
              const int az = base_z + a.second;
              const int bx = base_x + b.first;
              const int bz = base_z + b.second;
              const int da = (ax - origin_x) * (ax - origin_x) +
                             (az - origin_z) * (az - origin_z);
              const int db = (bx - origin_x) * (bx - origin_x) +
                             (bz - origin_z) * (bz - origin_z);
              return da < db;
            });

  for (size_t i = 0; i < column_count; ++i)
  {
    if (request.shouldCancel && (i % 4) == 0 && request.shouldCancel())
    {
      break;
    }
    const int lx = columns[i].first;
    const int lz = columns[i].second;
    const int world_x = base_x + lx;
    const int world_z = base_z + lz;
    if (auto *composable = dynamic_cast<UComposableWorldGenerator *>(pipeline))
    {
      UBlockWorldColumnWriter writer(genWorld, Registry);
      UColumnGenerationService::GenerateColumn(*composable, writer, world_x,
                                               world_z);
    }
    else
    {
      pipeline->GenerateColumn(world_x, world_z);
    }
  }

  if (auto *composable = dynamic_cast<UComposableWorldGenerator *>(pipeline))
  {
    if (settings.FillWater)
    {
      SealFluidPocketsInChunk(composable->GetContext(), base_x, base_z);
      if (SealFluidPermeableDecorInChunk(composable->GetContext(), base_x,
                                         base_z))
      {
        SealFluidPocketsInChunk(composable->GetContext(), base_x, base_z);
      }
    }
    PruneFloatingVegetationInChunk(composable->GetContext(), base_x, base_z);
  }

  genWorld.ForEachBlock(
      [&](glm::ivec3 pos, BlockId id)
      {
        result.buffer.SetBlock(pos, id);
        const uint8_t packed =
            PackFluidCellState(genWorld.GetFluidState(pos));
        if (packed != 0)
        {
          result.buffer.SetFluidPacked(pos, packed);
        }
      });
  return result;
}

} // namespace cutum
