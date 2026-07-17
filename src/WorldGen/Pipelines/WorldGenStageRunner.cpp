#include "WorldGen/Pipelines/WorldGenStageRunner.h"
#include "WorldGen/Pipelines/ComposableWorldGenerator.h"
#include "WorldGen/Core/WorldGenPack.h"
#include "WorldGen/Core/WorldGenStageMask.h"
#include "WorldGen/Features/BuiltinWorldGenFeatures.h"
#include "WorldGen/Features/CaveCarver.h"
#include "WorldGen/Features/OreVeinPlacer.h"
#include "WorldGen/Features/ObjectFeaturePlacer.h"
#include "WorldGen/Features/RavineCarver.h"
#include "WorldGen/Features/ValleyCarver.h"
#include "WorldGen/Core/WorldGenPack.h"
#include "WorldGen/Stages/WorldGenStages.h"
#include "WorldGen/Sampling/DensityFieldSampler.h"
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
                                 sample.SurfaceY, rule, *density_sampler);
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
  }
  const ValleySurfaceYCallback get_surface_y =
      [&generator](int hx, int hz) { return generator.SurfaceYAt(hx, hz); };
  CarveColumnValleys(ctx, world_x, world_z, sample.SurfaceY, ctx.Settings.Seed,
                     params, ctx.Settings.SeaLevel,
                     ctx.Settings.Tuning.riverWidth, get_surface_y);
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
      sample.SurfaceBiome);
}

void RunGroundCoverStage(UComposableWorldGenerator &generator,
                         const ColumnSampleContext &sample, int world_x,
                         int world_z, PostTerrainState &state)
{
  TryPlaceGroundCoverFeatures(generator.GetContext(), world_x, world_z,
                              sample.SurfaceY, sample.SurfaceBiome,
                              state.placed_tree);
}

void RunDecorationStage(UComposableWorldGenerator &generator,
                        const ColumnSampleContext &sample, int world_x,
                        int world_z, PostTerrainState &)
{
  TryPlaceDecorationFeatures(generator.GetContext(), world_x, world_z,
                             sample.SurfaceY, sample.SurfaceBiome);
}

void RunStructuresStage(UComposableWorldGenerator &generator,
                        const ColumnSampleContext &sample, int world_x,
                        int world_z, PostTerrainState &)
{
  TryPlaceStructureFeatures(generator.GetContext(), world_x, world_z,
                            sample.SurfaceY, sample.SurfaceBiome);
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
      {WorldGenStageId::Ravines, RunRavinesStage},
      {WorldGenStageId::Valleys, RunValleysStage},
      {WorldGenStageId::Caves, RunCavesStage},
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

void RunPostTerrainStagesExcluding(UComposableWorldGenerator &generator,
                                   const ColumnSampleContext &sample,
                                   int world_x, int world_z,
                                   uint32_t skip_stage_mask)
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
    RunStage(generator, sample, world_x, world_z, state, stage_id);
  }
}

void RunPostTerrainStages(UComposableWorldGenerator &generator,
                          const ColumnSampleContext &sample, int world_x,
                          int world_z)
{
  RunPostTerrainStagesExcluding(generator, sample, world_x, world_z, 0);
}

} // namespace cutum
