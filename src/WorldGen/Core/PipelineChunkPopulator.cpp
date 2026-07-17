#include "WorldGen/Core/IUChunkPopulator.h"
#include "World/Core/BlockWorld.h"
#include "World/Math/FluidCellState.h"
#include "World/Objects/ObjectLibrary.h"
#include "WorldGen/Core/BlockWorldColumnWriter.h"
#include "WorldGen/Core/IUWorldGenPipeline.h"
#include "WorldGen/Core/Noise.h"
#include "WorldGen/Core/WorldGenContentPin.h"
#include "WorldGen/Core/WorldGenPack.h"
#include "WorldGen/Core/WorldGenStageId.h"
#include "WorldGen/Features/CaveCarver.h"
#include "WorldGen/Features/ObjectFeaturePlacer.h"
#include "WorldGen/Features/RavineCarver.h"
#include "WorldGen/Features/ValleyCarver.h"
#include "WorldGen/Pipelines/ColumnGenerationService.h"
#include "WorldGen/Stages/MudflowErosion.h"
#include "WorldGen/Pipelines/ComposableWorldGenerator.h"
#include "WorldGen/Pipelines/WorldGenStageRunner.h"
#include "WorldGen/Stages/WorldGenStages.h"
#include "App/Platform/Log.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>

namespace cutum
{

namespace
{

std::mutex gPopulateDiagMutex;
ChunkPopulateTiming gLastPopulateTiming;

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

double ElapsedMs(std::chrono::steady_clock::time_point start)
{
  return std::chrono::duration<double, std::milli>(
             std::chrono::steady_clock::now() - start)
      .count();
}

} // namespace

void ChunkPopulateDiagnostics::Record(const ChunkPopulateTiming &timing)
{
  std::lock_guard<std::mutex> lock(gPopulateDiagMutex);
  gLastPopulateTiming = timing;
}

ChunkPopulateTiming ChunkPopulateDiagnostics::GetLast()
{
  std::lock_guard<std::mutex> lock(gPopulateDiagMutex);
  return gLastPopulateTiming;
}

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
  WorldGenContentSnapshot content = request.content.Pack
                                        ? request.content
                                        : CaptureWorldGenContentSnapshot();
  WorldGenContentPinScope pin(std::move(content));

  const auto populate_start = std::chrono::steady_clock::now();
  ChunkPopulateTiming timing{};

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

  std::array<std::array<ColumnSampleContext, CHUNK_SIZE>, CHUNK_SIZE> samples{};
  std::array<std::array<bool, CHUNK_SIZE>, CHUNK_SIZE> sample_valid{};

  auto *composable = dynamic_cast<UComposableWorldGenerator *>(pipeline);
  const auto terrain_start = std::chrono::steady_clock::now();
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
    if (composable)
    {
      UBlockWorldColumnWriter writer(genWorld, Registry);
      const ComposableTerrainMode terrain_mode = composable->GetConfig().TerrainMode;
      if (terrain_mode == ComposableTerrainMode::Flat ||
          terrain_mode == ComposableTerrainMode::LegacyHash)
      {
        UColumnGenerationService::GenerateColumn(*composable, writer, world_x,
                                                 world_z);
      }
      else
      {
        samples[lz][lx] = UColumnGenerationService::GenerateColumnTerrainOnly(
            *composable, writer, world_x, world_z);
        sample_valid[lz][lx] = true;
      }
    }
    else
    {
      pipeline->GenerateColumn(world_x, world_z);
    }
  }
  timing.terrainMs = ElapsedMs(terrain_start);
  // Terrain includes BuildColumnSample; expose as sample+terrain together for HUD.
  timing.sampleMs = timing.terrainMs;

  if (composable)
  {
    const ComposableTerrainMode terrain_mode = composable->GetConfig().TerrainMode;
    if (terrain_mode != ComposableTerrainMode::Flat &&
        terrain_mode != ComposableTerrainMode::LegacyHash)
    {
      WorldGenContext &ctx = composable->GetContext();
      const RavineSurfaceYCallback get_surface_y =
          [composable, base_x, base_z, &samples,
           &sample_valid](int hx, int hz)
          {
            const int lx = hx - base_x;
            const int lz = hz - base_z;
            if (lx >= 0 && lx < CHUNK_SIZE && lz >= 0 && lz < CHUNK_SIZE &&
                sample_valid[lz][lx])
            {
              return samples[lz][lx].SurfaceY;
            }
            return composable->SurfaceYAt(hx, hz);
          };

      const auto carve_start = std::chrono::steady_clock::now();
      if (settings.Tuning.useAnalyticValleys)
      {
        if (!(request.shouldCancel && request.shouldCancel()))
        {
          ValleyParams valley_params;
          const PackValleysConfig &pack = UWorldGenPack::ValleysConfig();
          if (pack.Loaded)
          {
            valley_params.enabled = pack.Enabled;
            valley_params.maxDepth = pack.MaxDepth;
            valley_params.widthSigma = pack.WidthSigma;
            valley_params.aquaticDepthScale = pack.AquaticDepthScale;
            valley_params.riverNoiseScale = pack.RiverNoiseScale;
          }
          CarveChunkValleys(ctx, base_x, base_z, settings.Seed, valley_params,
                            settings.SeaLevel, settings.Tuning.riverWidth,
                            get_surface_y);
        }
      }

      if (composable->GetStageMask().IsEnabled(WorldGenStageId::Ravines))
      {
        if (!(request.shouldCancel && request.shouldCancel()))
        {
          CarveChunkRavines(ctx, base_x, base_z, settings.Seed, settings.Ravines,
                            settings.SeaLevel, get_surface_y);
        }
      }
      timing.carveMs = ElapsedMs(carve_start);

      const uint32_t skip_chunk_stages =
          WorldGenStageSkipBit(WorldGenStageId::Valleys) |
          WorldGenStageSkipBit(WorldGenStageId::Ravines);

      const auto post_start = std::chrono::steady_clock::now();
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
        if (!sample_valid[lz][lx])
        {
          continue;
        }
        UBlockWorldColumnWriter writer(genWorld, Registry);
        UColumnGenerationService::GenerateColumnPostTerrain(
            *composable, writer, world_x, world_z, skip_chunk_stages,
            samples[lz][lx]);
      }
      timing.postMs = ElapsedMs(post_start);
    }

    const auto seal_start = std::chrono::steady_clock::now();
    if (settings.Tuning.useMudflowErosion)
    {
      ApplyMudflowToChunk(composable->GetContext(), base_x, base_z, 2);
    }
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
    timing.sealMs = ElapsedMs(seal_start);
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

  timing.totalMs = ElapsedMs(populate_start);
  ChunkPopulateDiagnostics::Record(timing);

  static thread_local double s_last_log_ms = 0.0;
  static thread_local auto s_last_log_tp = std::chrono::steady_clock::now();
  const auto now = std::chrono::steady_clock::now();
  const double since_log =
      std::chrono::duration<double, std::milli>(now - s_last_log_tp).count();
  if (timing.totalMs >= 40.0 || since_log >= 5000.0)
  {
    CubatariumLogInfo(
        "ChunkPopulate",
        "chunk=(" + std::to_string(request.chunkCoord.x) + "," +
            std::to_string(request.chunkCoord.y) + "," +
            std::to_string(request.chunkCoord.z) + ") total_ms=" +
            std::to_string(timing.totalMs) +
            " sample_ms=" + std::to_string(timing.sampleMs) +
            " terrain_ms=" + std::to_string(timing.terrainMs) +
            " carve_ms=" + std::to_string(timing.carveMs) +
            " post_ms=" + std::to_string(timing.postMs) +
            " seal_ms=" + std::to_string(timing.sealMs));
    s_last_log_tp = now;
    s_last_log_ms = timing.totalMs;
  }
  (void)s_last_log_ms;

  return result;
}

} // namespace cutum
