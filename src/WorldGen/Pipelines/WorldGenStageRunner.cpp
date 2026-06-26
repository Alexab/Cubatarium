#include "WorldGen/Pipelines/WorldGenStageRunner.h"
#include "WorldGen/Pipelines/ComposableWorldGenerator.h"
#include "WorldGen/Features/CaveCarver.h"
#include "WorldGen/Features/OreVeinPlacer.h"
#include "WorldGen/Features/PrefabFeaturePlacer.h"
#include "WorldGen/Features/RavineCarver.h"
#include "WorldGen/Stages/WorldGenStages.h"

namespace cutum
{

void RunTerrainStage(UComposableWorldGenerator &generator,
                     const ColumnSampleContext &sample, int world_x,
                     int world_z)
{
  const ColumnLayerRule rule =
      generator.BuildTerrainRuleFromSample(world_x, world_z, sample);
  FillTerrainColumn(generator.GetContext(), world_x, world_z, sample.SurfaceY,
                    rule);
}

void RunPostTerrainStages(UComposableWorldGenerator &generator,
                          const ColumnSampleContext &sample, int world_x,
                          int world_z)
{
  WorldGenContext &ctx = generator.GetContext();
  const ComposableWorldGenConfig &config = generator.GetConfig();
  const int surface_y = sample.SurfaceY;
  const BiomeId biome = sample.DominantBiome;
  const BiomeId ground_cover_biome = sample.SurfaceBiome;

  if (config.Ravines && ctx.Settings.Ravines.enabled)
  {
    CarveColumnRavines(ctx, world_x, world_z, surface_y, ctx.Settings.Seed,
                       ctx.Settings.Ravines);
  }
  if (config.Caves && ctx.Settings.EnableCaves)
  {
    CarveColumnCaves(ctx, world_x, world_z, surface_y, ctx.Settings.Seed,
                     ctx.Settings.Caves);
  }
  if (config.Fluids)
  {
    FillFluidColumn(ctx, world_x, world_z, surface_y);
  }
  if (config.Ores && ctx.Settings.EnableOres)
  {
    FillOreVeins(ctx, world_x, world_z, surface_y, ctx.Settings.Seed,
                 ctx.Settings.Tuning.oreDensity);
  }
  bool placed_tree = false;
  if (config.Vegetation && ctx.Settings.EnableTrees)
  {
    placed_tree =
        TryPlaceVegetationFeatures(ctx, world_x, world_z, surface_y, biome);
  }
  if (config.GroundCover && ctx.Settings.EnableGroundCover)
  {
    TryPlaceGroundCoverFeatures(ctx, world_x, world_z, surface_y,
                                ground_cover_biome, placed_tree);
  }
  if (config.Decoration)
  {
    TryPlaceDecorationFeatures(ctx, world_x, world_z, surface_y, biome);
  }
  if (config.Structures)
  {
    TryPlaceStructureFeatures(ctx, world_x, world_z, surface_y, biome);
  }
  if (config.LavaPools)
  {
    TryPlaceLavaPool(ctx, world_x, world_z, surface_y, biome);
  }
  if (config.FirePatch)
  {
    TryPlaceFirePatch(ctx, world_x, world_z, surface_y, biome, ctx.Grass);
  }
}

} // namespace cutum
