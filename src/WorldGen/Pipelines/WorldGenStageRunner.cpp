#include "WorldGen/Pipelines/WorldGenStageRunner.h"
#include "WorldGen/Pipelines/ComposableWorldGenerator.h"
#include "WorldGen/Core/WorldGenPack.h"
#include "WorldGen/Core/WorldGenStageMask.h"
#include "WorldGen/Features/BuiltinWorldGenFeatures.h"
#include "WorldGen/Features/CaveCarver.h"
#include "WorldGen/Features/OreVeinPlacer.h"
#include "WorldGen/Features/ObjectFeaturePlacer.h"
#include "WorldGen/Features/RavineCarver.h"
#include "WorldGen/Sampling/DensityFieldSampler.h"
#include "WorldGen/Stages/WorldGenStages.h"
#include <unordered_map>

namespace cutum
{

void RunTerrainStage(UComposableWorldGenerator &generator,
                     const ColumnSampleContext &sample, int world_x,
                     int world_z)
{
  const ColumnLayerRule rule =
      generator.BuildTerrainRuleFromSample(world_x, world_z, sample);
  WorldGenContext &ctx = generator.GetContext();
  if (generator.GetConfig().TerrainMode == ComposableTerrainMode::Density3D)
  {
    if (const UDensityFieldSampler *density_sampler =
            generator.GetDensitySampler())
    {
      FillTerrainColumnFromDensity(ctx, world_x, world_z, sample.SurfaceY, rule,
                                   *density_sampler);
      return;
    }
  }
  FillTerrainColumn(ctx, world_x, world_z, sample.SurfaceY, rule);
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
  CarveColumnRavines(ctx, world_x, world_z, sample.SurfaceY, ctx.Settings.Seed,
                     ctx.Settings.Ravines);
}

void RunCavesStage(UComposableWorldGenerator &generator,
                   const ColumnSampleContext &sample, int world_x, int world_z,
                   PostTerrainState &)
{
  WorldGenContext &ctx = generator.GetContext();
  if (generator.GetConfig().TerrainMode == ComposableTerrainMode::Density3D &&
      ctx.Settings.Caves.useDensityField)
  {
    return;
  }
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
      sample.DominantBiome);
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
                             sample.SurfaceY, sample.DominantBiome);
}

void RunStructuresStage(UComposableWorldGenerator &generator,
                        const ColumnSampleContext &sample, int world_x,
                        int world_z, PostTerrainState &)
{
  TryPlaceStructureFeatures(generator.GetContext(), world_x, world_z,
                            sample.SurfaceY, sample.DominantBiome);
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

void RunPostTerrainStages(UComposableWorldGenerator &generator,
                          const ColumnSampleContext &sample, int world_x,
                          int world_z)
{
  const WorldGenStageMask &mask = generator.GetStageMask();
  PostTerrainState state;
  for (WorldGenStageId stage_id : ResolvedStageOrder())
  {
    if (!mask.IsEnabled(stage_id))
    {
      continue;
    }
    RunStage(generator, sample, world_x, world_z, state, stage_id);
  }
}

} // namespace cutum
