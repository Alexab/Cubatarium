#include "WorldGen/Core/IUChunkPopulator.h"
#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"
#include "World/Objects/ObjectLibrary.h"
#include "World/Objects/ObjectUtil.h"
#include "WorldGen/Core/BlockWorldColumnWriter.h"
#include "WorldGen/Core/IUWorldGenPipeline.h"
#include "WorldGen/Core/Noise.h"
#include "WorldGen/Core/WorldGenContentPin.h"
#include "WorldGen/Core/WorldGenPack.h"
#include "WorldGen/Core/WorldGenBlockResolver.h"
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
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <vector>

namespace cutum
{

namespace
{

std::mutex gPopulateDiagMutex;
ChunkPopulateTiming gLastPopulateTiming;

struct PopulateMetricSamples
{
  std::vector<double> total;
  std::vector<double> sample;
  std::vector<double> terrain;
  std::vector<double> carve;
  std::vector<double> post;
  std::vector<double> seal;
  std::vector<double> mudflow;
  std::vector<double> pocketSeal;
  std::vector<double> permeableSeal;
  std::vector<double> permeableFloorScan;
  std::vector<double> permeableRestore;
  std::vector<double> permeableWaterlog;
  std::vector<double> pocketReseal;
  std::vector<double> vegetationPrune;
  std::array<std::vector<double>, WorldGenStageIdCount> postStages;
};

PopulateMetricSamples gPopulateSamples;
ChunkPopulateSummary gPopulateSummary;

void AppendPopulateMetric(std::vector<double> &samples, ChunkPopulateMetric &metric,
                          double value)
{
  samples.push_back(value);
  metric.totalMs += value;
  metric.maxMs = std::max(metric.maxMs, value);
}

void FinalizePopulateMetric(std::vector<double> &samples, ChunkPopulateMetric &metric)
{
  metric.count = samples.size();
  if (samples.empty())
  {
    return;
  }
  std::sort(samples.begin(), samples.end());
  metric.medianMs = samples[(samples.size() - 1) / 2];
  metric.p95Ms = samples[static_cast<size_t>(
      std::ceil(static_cast<double>(samples.size()) * 0.95) - 1.0)];
}

void FinalizePopulateSummary()
{
  FinalizePopulateMetric(gPopulateSamples.total, gPopulateSummary.total);
  FinalizePopulateMetric(gPopulateSamples.sample, gPopulateSummary.sample);
  FinalizePopulateMetric(gPopulateSamples.terrain, gPopulateSummary.terrain);
  FinalizePopulateMetric(gPopulateSamples.carve, gPopulateSummary.carve);
  FinalizePopulateMetric(gPopulateSamples.post, gPopulateSummary.post);
  FinalizePopulateMetric(gPopulateSamples.seal, gPopulateSummary.seal);
  FinalizePopulateMetric(gPopulateSamples.mudflow, gPopulateSummary.mudflow);
  FinalizePopulateMetric(gPopulateSamples.pocketSeal, gPopulateSummary.pocketSeal);
  FinalizePopulateMetric(gPopulateSamples.permeableSeal,
                         gPopulateSummary.permeableSeal);
  FinalizePopulateMetric(gPopulateSamples.permeableFloorScan,
                         gPopulateSummary.permeableFloorScan);
  FinalizePopulateMetric(gPopulateSamples.permeableRestore,
                         gPopulateSummary.permeableRestore);
  FinalizePopulateMetric(gPopulateSamples.permeableWaterlog,
                         gPopulateSummary.permeableWaterlog);
  FinalizePopulateMetric(gPopulateSamples.pocketReseal,
                         gPopulateSummary.pocketReseal);
  FinalizePopulateMetric(gPopulateSamples.vegetationPrune,
                         gPopulateSummary.vegetationPrune);
  for (size_t i = 0; i < gPopulateSamples.postStages.size(); ++i)
  {
    FinalizePopulateMetric(gPopulateSamples.postStages[i],
                           gPopulateSummary.postStages[i]);
  }
}

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

int FloraFeatureBlockRank(const std::string &blockName)
{
  if (blockName.find("leaves") != std::string::npos ||
      blockName.find("needles") != std::string::npos ||
      blockName.find("flower") != std::string::npos ||
      blockName.find("grass") != std::string::npos ||
      blockName.find("mushroom") != std::string::npos)
  {
    return 1;
  }
  if (blockName.find("stem") != std::string::npos ||
      blockName.find("branch") != std::string::npos)
  {
    return 2;
  }
  if (blockName.find("tree") != std::string::npos ||
      blockName.find("log") != std::string::npos ||
      blockName.find("trunk") != std::string::npos ||
      blockName.find("bark") != std::string::npos)
  {
    return 3;
  }
  return 1;
}

/// RAII: mirror gen-scratch SetBlock/SetFluid into the transfer ChunkBuffer.
struct CaptureBufferScope
{
  UBlockWorld &World;
  explicit CaptureBufferScope(UBlockWorld &world, UChunkBuffer &buffer,
                              glm::ivec3 ownerChunk,
                              const UBlockRegistry &registry,
                              const std::vector<std::pair<BlockId, int>>
                                  &treeBlockRanks)
      : World(world)
  {
    buffer.SetWorldGenOwnerChunk(ownerChunk, registry, treeBlockRanks);
    World.SetCaptureBuffer(&buffer);
  }
  ~CaptureBufferScope() { World.SetCaptureBuffer(nullptr); }
  CaptureBufferScope(const CaptureBufferScope &) = delete;
  CaptureBufferScope &operator=(const CaptureBufferScope &) = delete;
};

} // namespace

/// Carry deterministic priorities for all flora voxels across chunk borders.
std::vector<std::pair<BlockId, int>> CollectFloraFeatureBlockRanks(
    UBlockRegistry &registry, UObjectLibrary *objects,
    const std::string &ownerPackId, const ObjectFeatureConfig *features)
{
  std::vector<std::pair<BlockId, int>> ranks;
  if (!features)
  {
    return ranks;
  }
  const auto addBlock = [&](BlockId id, int rank)
  {
    if (id == BLOCK_AIR || rank <= 0)
    {
      return;
    }
    auto found = std::find_if(
        ranks.begin(), ranks.end(),
        [id](const std::pair<BlockId, int> &entry) { return entry.first == id; });
    if (found == ranks.end())
    {
      ranks.emplace_back(id, rank);
    }
    else
    {
      found->second = std::max(found->second, rank);
    }
  };
  const auto collectPool = [&](const std::vector<ObjectFeatureRule> &rules)
  {
    for (const ObjectFeatureRule &rule : rules)
    {
      if (rule.Mode == ObjectFeatureRuleMode::ProceduralTree)
      {
        addBlock(ResolvePackScatterBlockId(registry, ownerPackId,
                                           rule.Tree.TrunkBlock),
                 3);
        addBlock(ResolvePackScatterBlockId(registry, ownerPackId,
                                           rule.Tree.LeavesBlock),
                 1);
        continue;
      }
      if (rule.Mode == ObjectFeatureRuleMode::ProceduralShrub)
      {
        addBlock(ResolvePackScatterBlockId(registry, ownerPackId,
                                           rule.Shrub.StemBlock),
                 2);
        addBlock(ResolvePackScatterBlockId(registry, ownerPackId,
                                           rule.Shrub.LeavesBlock),
                 1);
        addBlock(ResolvePackScatterBlockId(registry, ownerPackId,
                                           rule.Shrub.AccentBlock),
                 1);
        continue;
      }
      if (rule.Mode == ObjectFeatureRuleMode::ScatterBlocks)
      {
        addBlock(ResolvePackScatterBlockId(registry, ownerPackId,
                                           rule.Scatter.BlockName),
                 1);
        continue;
      }
      if (rule.Mode != ObjectFeatureRuleMode::Object || !objects)
      {
        continue;
      }
      const auto object = objects->GetShared(rule.ObjectName);
      if (!object)
      {
        continue;
      }
      for (const ObjectVoxel &voxel : object->voxels)
      {
        const BlockId id = ResolveObjectVoxelPlacementId(voxel, registry);
        if (id == BLOCK_AIR)
        {
          continue;
        }
        addBlock(id, FloraFeatureBlockRank(registry.GetTypeNameById(id)));
      }
    }
  };
  collectPool(features->Vegetation);
  collectPool(features->GroundCover);
  // Decoration (surface logs/ruins) and Structures write through the same
  // capture buffer; without ranks their pack-specific voxels would be
  // treated as terrain by cross-chunk conflict rules.
  collectPool(features->Decoration);
  collectPool(features->Structures);
  return ranks;
}

void ChunkPopulateDiagnostics::Record(const ChunkPopulateTiming &timing,
                                      bool discarded)
{
  std::lock_guard<std::mutex> lock(gPopulateDiagMutex);
  gLastPopulateTiming = timing;
  if (discarded)
  {
    ++gPopulateSummary.discardedCount;
    return;
  }
  ++gPopulateSummary.completedCount;
  AppendPopulateMetric(gPopulateSamples.total, gPopulateSummary.total, timing.totalMs);
  AppendPopulateMetric(gPopulateSamples.sample, gPopulateSummary.sample,
                       timing.sampleMs);
  AppendPopulateMetric(gPopulateSamples.terrain, gPopulateSummary.terrain,
                       timing.terrainMs);
  AppendPopulateMetric(gPopulateSamples.carve, gPopulateSummary.carve, timing.carveMs);
  AppendPopulateMetric(gPopulateSamples.post, gPopulateSummary.post, timing.postMs);
  AppendPopulateMetric(gPopulateSamples.seal, gPopulateSummary.seal, timing.sealMs);
  if (timing.detailEnabled)
  {
    ++gPopulateSummary.detailCount;
    AppendPopulateMetric(gPopulateSamples.mudflow, gPopulateSummary.mudflow,
                         timing.mudflowMs);
    AppendPopulateMetric(gPopulateSamples.pocketSeal, gPopulateSummary.pocketSeal,
                         timing.pocketSealMs);
    AppendPopulateMetric(gPopulateSamples.permeableSeal,
                         gPopulateSummary.permeableSeal, timing.permeableSealMs);
    AppendPopulateMetric(gPopulateSamples.permeableFloorScan,
                         gPopulateSummary.permeableFloorScan,
                         timing.permeableFloorScanMs);
    AppendPopulateMetric(gPopulateSamples.permeableRestore,
                         gPopulateSummary.permeableRestore,
                         timing.permeableRestoreMs);
    AppendPopulateMetric(gPopulateSamples.permeableWaterlog,
                         gPopulateSummary.permeableWaterlog,
                         timing.permeableWaterlogMs);
    AppendPopulateMetric(gPopulateSamples.pocketReseal,
                         gPopulateSummary.pocketReseal, timing.pocketResealMs);
    AppendPopulateMetric(gPopulateSamples.vegetationPrune,
                         gPopulateSummary.vegetationPrune,
                         timing.vegetationPruneMs);
    for (size_t i = 0; i < timing.postStageMs.size(); ++i)
    {
      AppendPopulateMetric(gPopulateSamples.postStages[i],
                           gPopulateSummary.postStages[i], timing.postStageMs[i]);
    }
  }
  gPopulateSummary.mudflowColumns += timing.mudflowColumns;
  gPopulateSummary.mudflowVoxelsMoved += timing.mudflowVoxelsMoved;
  gPopulateSummary.pocketVoxelsFilled += timing.pocketVoxelsFilled;
  gPopulateSummary.permeableColumns += timing.permeableColumns;
  gPopulateSummary.permeableColumnsChanged += timing.permeableColumnsChanged;
  gPopulateSummary.permeableFloorGroundChecks += timing.permeableFloorGroundChecks;
  gPopulateSummary.permeableFloorSlotChecks += timing.permeableFloorSlotChecks;
  gPopulateSummary.permeableRestoreCells += timing.permeableRestoreCells;
  gPopulateSummary.permeableWaterlogCells += timing.permeableWaterlogCells;
  gPopulateSummary.resealVoxelsFilled += timing.resealVoxelsFilled;
  gPopulateSummary.pruneColumns += timing.pruneColumns;
  gPopulateSummary.prunedVoxels += timing.prunedVoxels;
}

ChunkPopulateTiming ChunkPopulateDiagnostics::GetLast()
{
  std::lock_guard<std::mutex> lock(gPopulateDiagMutex);
  return gLastPopulateTiming;
}

ChunkPopulateSummary ChunkPopulateDiagnostics::GetSummary()
{
  std::lock_guard<std::mutex> lock(gPopulateDiagMutex);
  FinalizePopulateSummary();
  return gPopulateSummary;
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
  const bool audit_seal =
      std::getenv("CUBATARIUM_WORLDGEN_SEAL_AUDIT") != nullptr;
  timing.detailEnabled = audit_seal;

  ChunkPopulateResult result;
  result.coord = request.chunkCoord;
  result.token = request.token;

  ProceduralSettings settings = request.settings;
  if (settings.EnableCaves &&
      settings.TerrainBackendMode != TerrainBackend::Density3D &&
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
  ObjectFeatureChunkDiagnosticsScope objectFeatureDiagnostics(audit_seal);

  // D1: write gen scratch + transfer buffer together (no final world→buffer scan).
  result.buffer.Clear();
  CaptureBufferScope capture_scope(genWorld, result.buffer, request.chunkCoord,
                                   Registry,
                                   CollectFloraFeatureBlockRanks(
                                       Registry, objects, WorldgenOwnerPackId,
                                       pin.Get().Features.get()));

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
  bool cancelled = false;
  if (composable)
  {
    const int blend_pad = std::clamp(
        static_cast<int>(
            std::lround(settings.Tuning.biomeBlendRadius)),
        0, 16);
    // blend + coast scan (4) + smoothing neighborhood margin
    const int pad = blend_pad + 8;
    composable->BeginChunkCoarseCache(base_x, base_z, pad);
  }
  const auto terrain_start = std::chrono::steady_clock::now();
  double sample_ms = 0.0;
  double fill_ms = 0.0;
  for (size_t i = 0; i < column_count; ++i)
  {
    if (request.shouldCancel && (i % 4) == 0 && request.shouldCancel())
    {
      cancelled = true;
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
            *composable, writer, world_x, world_z, &sample_ms, &fill_ms);
        sample_valid[lz][lx] = true;
      }
    }
    else
    {
      pipeline->GenerateColumn(world_x, world_z);
    }
  }
  timing.terrainMs = ElapsedMs(terrain_start);
  timing.sampleMs = sample_ms > 0.0 ? sample_ms : timing.terrainMs;
  if (fill_ms > 0.0)
  {
    timing.terrainMs = fill_ms;
  }

  if (cancelled || (request.shouldCancel && request.shouldCancel()))
  {
    if (composable)
    {
      composable->EndChunkCoarseCache();
    }
    result.discarded = true;
    result.buffer.Clear();
    timing.totalMs = ElapsedMs(populate_start);
    ChunkPopulateDiagnostics::Record(timing, result.discarded);
    return result;
  }

  if (composable)
  {
    const auto seal_cancelled = [&]() -> bool
    {
      return request.shouldCancel && request.shouldCancel();
    };
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
            valley_params.fillWater = pack.FillWater;
            valley_params.waterMinStrength = pack.WaterMinStrength;
            valley_params.waterMinMoisture = pack.WaterMinMoisture;
            valley_params.waterSurfaceDepth = pack.WaterSurfaceDepth;
            valley_params.maxWaterElevationAboveSea =
                pack.WaterMaxElevationAboveSea;
          }
          if (settings.WorldGenProfileVersion >= 3)
          {
            valley_params.lowlandOnly = true;
            valley_params.warpBlocks = 36.0f;
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

      // Mudflow runs BEFORE the post-terrain stages: vegetation must root on
      // the post-erosion surface, otherwise erosion undercuts freshly placed
      // trunks and leaves them standing on air.
      if (settings.Tuning.useMudflowErosion && !seal_cancelled())
      {
        const auto stage_start = audit_seal
                                     ? std::chrono::steady_clock::now()
                                     : std::chrono::steady_clock::time_point{};
        int moved = 0;
        ApplyMudflowToChunk(composable->GetContext(), base_x, base_z, 2, &moved);
        timing.mudflowColumns += CHUNK_SIZE * CHUNK_SIZE;
        timing.mudflowVoxelsMoved += static_cast<uint64_t>(moved);
        if (audit_seal)
        {
          timing.mudflowMs = ElapsedMs(stage_start);
        }
      }

      const uint32_t skip_chunk_stages =
          WorldGenStageSkipBit(WorldGenStageId::Valleys) |
          WorldGenStageSkipBit(WorldGenStageId::Ravines);

      const auto post_start = std::chrono::steady_clock::now();
      // Tree/object placement can reject a feature when an earlier feature
      // already occupies its footprint. Use a canonical order here so the
      // result does not depend on the player-centered order used to prime the
      // terrain cache above.
      size_t post_index = 0;
      for (int lz = 0; lz < CHUNK_SIZE && !cancelled; ++lz)
      {
        for (int lx = 0; lx < CHUNK_SIZE; ++lx, ++post_index)
        {
          if (request.shouldCancel && (post_index % 4) == 0 &&
              request.shouldCancel())
          {
            cancelled = true;
            break;
          }
          if (!sample_valid[lz][lx])
          {
            continue;
          }
          const int world_x = base_x + lx;
          const int world_z = base_z + lz;
          UBlockWorldColumnWriter writer(genWorld, Registry);
          UColumnGenerationService::GenerateColumnPostTerrain(
              *composable, writer, world_x, world_z, skip_chunk_stages,
              samples[lz][lx], audit_seal ? &timing.postStageMs : nullptr);
        }
      }
      timing.postMs = ElapsedMs(post_start);
    }

    if (cancelled || (request.shouldCancel && request.shouldCancel()))
    {
      if (composable)
      {
        composable->EndChunkCoarseCache();
      }
      result.discarded = true;
      result.buffer.Clear();
      timing.totalMs = ElapsedMs(populate_start);
      ChunkPopulateDiagnostics::Record(timing, result.discarded);
      return result;
    }

    const auto seal_start = std::chrono::steady_clock::now();
    // Pocket seal on worker (with shouldCancel). Main only drains cheap ShoreAir.
    if (settings.FillWater && !seal_cancelled())
    {
      auto stage_start = audit_seal
                             ? std::chrono::steady_clock::now()
                             : std::chrono::steady_clock::time_point{};
      int filled = 0;
      SealFluidPocketsInChunk(composable->GetContext(), base_x, base_z, &filled);
      timing.pocketVoxelsFilled = static_cast<uint64_t>(filled);
      if (audit_seal)
      {
        timing.pocketSealMs = ElapsedMs(stage_start);
      }
      if (!seal_cancelled())
      {
        stage_start = audit_seal ? std::chrono::steady_clock::now()
                                 : std::chrono::steady_clock::time_point{};
        int columns_changed = 0;
        FluidPermeableSealStats permeableStats;
        const bool permeable_changed = SealFluidPermeableDecorInChunk(
            composable->GetContext(), base_x, base_z, &columns_changed,
            audit_seal ? &permeableStats : nullptr);
        timing.permeableColumns = CHUNK_SIZE * CHUNK_SIZE;
        timing.permeableColumnsChanged = static_cast<uint64_t>(columns_changed);
        timing.permeableFloorScanMs = permeableStats.floorScanMs;
        timing.permeableRestoreMs = permeableStats.restoreMs;
        timing.permeableWaterlogMs = permeableStats.waterlogMs;
        timing.permeableFloorGroundChecks = permeableStats.floorGroundChecks;
        timing.permeableFloorSlotChecks = permeableStats.floorSlotChecks;
        timing.permeableRestoreCells = permeableStats.restoreCells;
        timing.permeableWaterlogCells = permeableStats.waterlogCells;
        if (audit_seal)
        {
          timing.permeableSealMs = ElapsedMs(stage_start);
        }
        if (permeable_changed && !seal_cancelled())
        {
          stage_start = audit_seal ? std::chrono::steady_clock::now()
                                   : std::chrono::steady_clock::time_point{};
          filled = 0;
          SealFluidPocketsInChunk(composable->GetContext(), base_x, base_z, &filled);
          timing.resealVoxelsFilled = static_cast<uint64_t>(filled);
          if (audit_seal)
          {
            timing.pocketResealMs = ElapsedMs(stage_start);
          }
        }
      }
      result.fluidSealed = !seal_cancelled();
    }
    if (seal_cancelled())
    {
      if (composable)
      {
        composable->EndChunkCoarseCache();
      }
      result.discarded = true;
      result.buffer.Clear();
      timing.sealMs = ElapsedMs(seal_start);
      timing.totalMs = ElapsedMs(populate_start);
      ChunkPopulateDiagnostics::Record(timing, result.discarded);
      return result;
    }
    const auto prune_start = audit_seal
                                 ? std::chrono::steady_clock::now()
                                 : std::chrono::steady_clock::time_point{};
    timing.prunedVoxels = static_cast<uint64_t>(PruneFloatingVegetationInChunk(
        composable->GetContext(), base_x, base_z));
    timing.pruneColumns = CHUNK_SIZE * CHUNK_SIZE;
    if (audit_seal)
    {
      timing.vegetationPruneMs = ElapsedMs(prune_start);
    }
    timing.sealMs = ElapsedMs(seal_start);
  }

  if (composable)
  {
    composable->EndChunkCoarseCache();
  }
  // result.buffer already mirrors genWorld via CaptureBufferScope (D1).

  timing.totalMs = ElapsedMs(populate_start);
  ChunkPopulateDiagnostics::Record(timing, result.discarded);

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
            " seal_ms=" + std::to_string(timing.sealMs) +
            (audit_seal
                 ? (" mudflow_ms=" + std::to_string(timing.mudflowMs) +
                    " pocket_ms=" + std::to_string(timing.pocketSealMs) +
                    " permeable_ms=" +
                        std::to_string(timing.permeableSealMs) +
                    " reseal_ms=" + std::to_string(timing.pocketResealMs) +
                    " prune_ms=" + std::to_string(timing.vegetationPruneMs))
                 : std::string()));
    s_last_log_tp = now;
    s_last_log_ms = timing.totalMs;
  }
  (void)s_last_log_ms;

  return result;
}

} // namespace cutum
