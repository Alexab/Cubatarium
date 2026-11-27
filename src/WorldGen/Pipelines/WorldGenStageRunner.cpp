#include "WorldGen/Pipelines/WorldGenStageRunner.h"
#include "WorldGen/Pipelines/ComposableWorldGenerator.h"
#include "WorldGen/Core/WorldGenPack.h"
#include "WorldGen/Core/WorldGenStageMask.h"
#include "WorldGen/Features/BuiltinWorldGenFeatures.h"
#include "WorldGen/Features/CaveCarver.h"
#include "WorldGen/Features/LakeCarver.h"
#include "WorldGen/Features/OreVeinPlacer.h"
#include "WorldGen/Features/ObjectFeaturePlacer.h"
#include "WorldGen/Features/RavineCarver.h"
#include "WorldGen/Features/ValleyCarver.h"
#include "WorldGen/Core/WorldGenPack.h"
#include "WorldGen/Stages/WorldGenStages.h"
#include "WorldGen/Sampling/DensityFieldSampler.h"
#include <chrono>
#include <unordered_map>

namespace cutum
{

void RunTerrainStage(UComposableWorldGenerator &generator,
                     const ColumnSampleContext &sample, int world_x,
                     int world_z)
{
  const ColumnLayerRule rule =
      generator.BuildTerrainRuleFromSample(world_x, world_z, sample);
  if (generator.GetConfig().TerrainMode == ComposableTerrainMode::Density3D)
  {
    if (const UDensityFieldSampler *density_sampler =
            generator.GetDensitySampler())
    {
      FillTerrainColumnFromDensity(generator.GetContext(), world_x, world_z,
                                 sample.SurfaceY, sample.RawSurfaceY, rule,
                                 *density_sampler,
                                 generator.GetAquiferSampler());
      return;
    }
  }
  FillTerrainColumn(generator.GetContext(), world_x, world_z, sample.SurfaceY,
                    rule);
}

namespace
{

struct PostTerrainState
{
  bool placed_tree{false};
};

using PostTerrainStageFn = void (*)(UComposableWorldGenerator &,
                                    const ColumnSampleContext &, int, int,
                                    PostTerrainState &);

void RunGeologyStage(UComposableWorldGenerator &generator,
                     const ColumnSampleContext &sample, int world_x,
                     int world_z, PostTerrainState &)
{
  WorldGenContext &ctx = generator.GetContext();
  ApplyGeologyColumn(ctx, world_x, world_z, sample.SurfaceY,
                     ctx.Settings.Seed);
}

void RunRavinesStage(UComposableWorldGenerator &generator,
                     const ColumnSampleContext &sample, int world_x,
                     int world_z, PostTerrainState &)
{
  WorldGenContext &ctx = generator.GetContext();
  const RavineSurfaceYCallback get_surface_y =
      [&generator](int hx, int hz) { return generator.SurfaceYAt(hx, hz); };
  CarveColumnRavines(ctx, world_x, world_z, sample.SurfaceY, ctx.Settings.Seed,
                     ctx.Settings.Ravines, ctx.Settings.SeaLevel,
                     get_surface_y);
}

void RunValleysStage(UComposableWorldGenerator &generator,
                     const ColumnSampleContext &sample, int world_x,
                     int world_z, PostTerrainState &)
{
  WorldGenContext &ctx = generator.GetContext();
  ValleyParams params;
  const PackValleysConfig &pack = UWorldGenPack::ValleysConfig();
  if (pack.Loaded)
  {
    params.enabled = pack.Enabled;
    params.maxDepth = pack.MaxDepth;
    params.widthSigma = pack.WidthSigma;
    params.aquaticDepthScale = pack.AquaticDepthScale;
    params.riverNoiseScale = pack.RiverNoiseScale;
    params.fillWater = pack.FillWater;
    params.waterMinStrength = pack.WaterMinStrength;
    params.waterMinMoisture = pack.WaterMinMoisture;
    params.waterSurfaceDepth = pack.WaterSurfaceDepth;
    params.maxWaterElevationAboveSea = pack.WaterMaxElevationAboveSea;
  }
  if (ctx.Settings.WorldGenProfileVersion >= 3)
  {
    params.lowlandOnly = true;
    params.warpBlocks = 36.0f;
  }
  const ValleySurfaceYCallback get_surface_y =
      [&generator](int hx, int hz) { return generator.SurfaceYAt(hx, hz); };
  CarveColumnValleys(ctx, world_x, world_z, sample.SurfaceY, ctx.Settings.Seed,
                     params, ctx.Settings.SeaLevel,
                     ctx.Settings.Tuning.riverWidth, get_surface_y);
}

void RunLakesStage(UComposableWorldGenerator &generator,
                   const ColumnSampleContext &sample, int world_x, int world_z,
                   PostTerrainState &)
{
  WorldGenContext &ctx = generator.GetContext();
  LakeParams params;
  const PackLakesConfig &pack = UWorldGenPack::LakesConfig();
  if (pack.Loaded)
  {
    params.enabled = pack.Enabled;
    params.cellSize = pack.CellSize;
    params.chancePerCell = pack.ChancePerCell;
    params.radiusMin = pack.RadiusMin;
    params.radiusMax = pack.RadiusMax;
    params.minElevationAboveSea = pack.MinElevationAboveSea;
    params.maxElevationAboveSea = pack.MaxElevationAboveSea;
    params.maxExcavationDepth = pack.MaxExcavationDepth;
    params.basinDepthMin = pack.BasinDepthMin;
    params.basinDepthMax = pack.BasinDepthMax;
    params.minContinentalness = pack.MinContinentalness;
    params.maxContinentalness = pack.MaxContinentalness;
    params.minMoisture = pack.MinMoisture;
    params.maxTemperature = pack.MaxTemperature;
  }
  const LakeSurfaceYCallback get_surface_y = [&generator](int hx, int hz)
  { return generator.LakeCenterSurfaceYAt(hx, hz); };
  CarveLakeColumn(ctx, world_x, world_z, sample.SurfaceY, ctx.Settings.Seed,
                  params, ctx.Settings.SeaLevel, get_surface_y);
}

void RunCavesStage(UComposableWorldGenerator &generator,
                   const ColumnSampleContext &sample, int world_x, int world_z,
                   PostTerrainState &)
{
  WorldGenContext &ctx = generator.GetContext();
  CarveColumnCaves(ctx, world_x, world_z, sample.SurfaceY, ctx.Settings.Seed,
                   ctx.Settings.Caves);
}

void RunFluidsStage(UComposableWorldGenerator &generator,
                    const ColumnSampleContext &sample, int world_x, int world_z,
                    PostTerrainState &)
{
  FillFluidColumn(generator.GetContext(), world_x, world_z, sample.SurfaceY);
}

void RunAquifersStage(UComposableWorldGenerator &generator,
                      const ColumnSampleContext &sample, int world_x,
                      int world_z, PostTerrainState &)
{
  const UAquiferSampler *sampler = generator.GetAquiferSampler();
  if (sampler != nullptr)
  {
    WorldGenContext &ctx = generator.GetContext();
    FillAquiferColumn(ctx, world_x, world_z, sample.SurfaceY, *sampler);
  }
}

void RunCaveFeaturesStage(UComposableWorldGenerator &generator,
                          const ColumnSampleContext &sample, int world_x,
                          int world_z, PostTerrainState &)
{
  const auto biome_at_3d = [&generator](int x, int y, int z, int surface_y)
  { return generator.BiomeAt3D(x, y, z, surface_y); };
  const auto biome_key_at_3d = [&generator](int x, int y, int z, int surface_y)
  { return generator.BiomeKeyRefAt3D(x, y, z, surface_y); };
  TryPlaceCaveFeaturesRef(generator.GetContext(), world_x, world_z,
                       sample.SurfaceY, biome_at_3d, biome_key_at_3d);
}

void RunOresStage(UComposableWorldGenerator &generator,
                  const ColumnSampleContext &sample, int world_x, int world_z,
                  PostTerrainState &)
{
  WorldGenContext &ctx = generator.GetContext();
  FillOreVeins(ctx, world_x, world_z, sample.SurfaceY, ctx.Settings.Seed,
               ctx.Settings.Tuning.oreDensity);
}

void RunVegetationStage(UComposableWorldGenerator &generator,
                        const ColumnSampleContext &sample, int world_x,
                        int world_z, PostTerrainState &state)
{
  state.placed_tree = TryPlaceVegetationFeatures(
      generator.GetContext(), world_x, world_z, sample.SurfaceY,
      sample.SurfaceBiome, sample.SurfaceBiomeKey);
}

void RunGroundCoverStage(UComposableWorldGenerator &generator,
                         const ColumnSampleContext &sample, int world_x,
                         int world_z, PostTerrainState &state)
{
  TryPlaceGroundCoverFeatures(generator.GetContext(), world_x, world_z,
                              sample.SurfaceY, sample.SurfaceBiome,
                              state.placed_tree, sample.SurfaceBiomeKey);
}

void RunDecorationStage(UComposableWorldGenerator &generator,
                        const ColumnSampleContext &sample, int world_x,
                        int world_z, PostTerrainState &)
{
  TryPlaceDecorationFeatures(generator.GetContext(), world_x, world_z,
                             sample.SurfaceY, sample.SurfaceBiome,
                             sample.SurfaceBiomeKey);
}

void RunStructuresStage(UComposableWorldGenerator &generator,
                        const ColumnSampleContext &sample, int world_x,
                        int world_z, PostTerrainState &)
{
  TryPlaceStructureFeatures(generator.GetContext(), world_x, world_z,
                            sample.SurfaceY, sample.SurfaceBiome,
                            sample.SurfaceBiomeKey);
}

void RunBuiltinStage(UComposableWorldGenerator &generator,
                     const ColumnSampleContext &sample, int world_x, int world_z,
                     PostTerrainState &, WorldGenStageId stage_id)
{
  if (const IUBuiltinWorldGenFeature *feature =
          BuiltinWorldGenFeatureFor(stage_id))
  {
    feature->TryPlace(generator.GetContext(), sample, world_x, world_z);
  }
}

const std::unordered_map<WorldGenStageId, PostTerrainStageFn> &StageFnMap()
{
  static const std::unordered_map<WorldGenStageId, PostTerrainStageFn> map = {
      {WorldGenStageId::Geology, RunGeologyStage},
      {WorldGenStageId::Ravines, RunRavinesStage},
      {WorldGenStageId::Valleys, RunValleysStage},
      {WorldGenStageId::Lakes, RunLakesStage},
      {WorldGenStageId::Caves, RunCavesStage},
      {WorldGenStageId::Aquifers, RunAquifersStage},
      {WorldGenStageId::CaveFeatures, RunCaveFeaturesStage},
      {WorldGenStageId::Fluids, RunFluidsStage},
      {WorldGenStageId::Ores, RunOresStage},
      {WorldGenStageId::Vegetation, RunVegetationStage},
      {WorldGenStageId::GroundCover, RunGroundCoverStage},
      {WorldGenStageId::Decoration, RunDecorationStage},
      {WorldGenStageId::Structures, RunStructuresStage},
  };
  return map;
}

void RunStage(UComposableWorldGenerator &generator,
              const ColumnSampleContext &sample, int world_x, int world_z,
              PostTerrainState &state, WorldGenStageId stage_id)
{
  if (BuiltinWorldGenFeatureFor(stage_id))
  {
    RunBuiltinStage(generator, sample, world_x, world_z, state, stage_id);
    return;
  }
  const auto &map = StageFnMap();
  const auto it = map.find(stage_id);
  if (it != map.end())
  {
    it->second(generator, sample, world_x, world_z, state);
  }
}

const std::vector<WorldGenStageId> &ResolvedStageOrder()
{
  const WorldGenPackPipeline &pipeline = UWorldGenPack::Get().Pipeline;
  if (pipeline.Loaded && !pipeline.StageOrder.empty())
  {
    return pipeline.StageOrder;
  }
  static const std::vector<WorldGenStageId> kDefault =
      DefaultPostTerrainStageOrder();
  return kDefault;
}

} // namespace

uint32_t WorldGenStageSkipBit(WorldGenStageId id)
{
  return 1u << static_cast<uint32_t>(id);
}

std::optional<WorldGenWriteExtents>
GetWorldGenWriteExtents(const UComposableWorldGenerator &generator)
{
  const auto mode = generator.GetConfig().TerrainMode;
  if (mode == ComposableTerrainMode::Flat || mode == ComposableTerrainMode::LegacyHash)
    return WorldGenWriteExtents{};

  const auto &ctx = generator.GetContext();
  const auto &settings = ctx.Settings;
  const auto &mask = generator.GetStageMask();
  WorldGenWriteExtent common;
  int ravine_reach = 0;
  for (const auto stage : ResolvedStageOrder())
  {
    if (!mask.IsEnabled(stage))
      continue;
    std::optional<ObjectFeaturePool> pool;
    switch (stage)
    {
    case WorldGenStageId::Ravines:
      ravine_reach = MaxRavineColumnWriteReach(settings.Ravines, settings.WorldGenProfileVersion);
      break;
    case WorldGenStageId::Vegetation:
      if (settings.EnableTrees)
        pool = ObjectFeaturePool::Vegetation;
      break;
    case WorldGenStageId::GroundCover:
      if (settings.EnableGroundCover)
        pool = ObjectFeaturePool::GroundCover;
      break;
    case WorldGenStageId::Decoration:
      if (settings.EnableDecoration)
        pool = ObjectFeaturePool::Decoration;
      break;
    case WorldGenStageId::Structures:
      if (settings.EnableStructures)
        pool = ObjectFeaturePool::Structures;
      break;
    case WorldGenStageId::LavaPools:
      if (settings.FillLava)
        common.IncludeRadius(1, 1);
      break;
    case WorldGenStageId::FirePatch:
      if (settings.FillFire)
        common.Include(GetPrefabWriteExtent(ctx, "fire_patch"));
      break;
    case WorldGenStageId::CaveFeatures:
      if (settings.EnableCaves && !CaveFeaturesWriteWithinTerrainColumn(ctx))
        return std::nullopt;
      break;
    default:
      // Terrain, valleys, lakes, caves, fluids, aquifers and ores write to
      // the requested XZ. Geology's geode is aligned inside its source chunk.
      break;
    }
    if (pool)
    {
      const auto extent = GetObjectFeatureWriteExtent(ctx, *pool);
      if (!extent)
        return std::nullopt;
      common.Include(*extent);
    }
  }

  // Populate replaces the column ravine stage with a chunk pass. That pass,
  // the chunk valley and mudflow passes, and the seals stay in the owner XZ
  // rectangle. Pruning can only remove existing blocks.
  WorldGenWriteExtents result{common, common};
  result.synchronous_sample.IncludeRadius(ravine_reach, ravine_reach);
  return result;
}

void RunPostTerrainStagesExcluding(
    UComposableWorldGenerator &generator, const ColumnSampleContext &sample,
    int world_x, int world_z, uint32_t skip_stage_mask,
    std::array<double, WorldGenStageIdCount> *out_stage_ms)
{
  const WorldGenStageMask &mask = generator.GetStageMask();
  PostTerrainState state;
  for (WorldGenStageId stage_id : ResolvedStageOrder())
  {
    if ((skip_stage_mask & WorldGenStageSkipBit(stage_id)) != 0)
    {
      continue;
    }
    if (!mask.IsEnabled(stage_id))
    {
      continue;
    }
    if (out_stage_ms)
    {
      const auto start = std::chrono::steady_clock::now();
      RunStage(generator, sample, world_x, world_z, state, stage_id);
      (*out_stage_ms)[static_cast<size_t>(stage_id)] +=
          std::chrono::duration<double, std::milli>(
              std::chrono::steady_clock::now() - start)
              .count();
    }
    else
    {
      RunStage(generator, sample, world_x, world_z, state, stage_id);
    }
  }
}

void RunPostTerrainStages(UComposableWorldGenerator &generator,
                          const ColumnSampleContext &sample, int world_x,
                          int world_z)
{
  RunPostTerrainStagesExcluding(generator, sample, world_x, world_z, 0);
}

} // namespace cutum
