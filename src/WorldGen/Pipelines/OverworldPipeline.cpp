#include "WorldGen/Pipelines/OverworldPipeline.h"
#include "WorldGen/Stages/WorldGenStages.h"

namespace cutum
{

UOverworldPipeline::UOverworldPipeline(WorldGenContext ctx, HeightPreset preset)
    : IWorldGenPipeline(ctx),
      HeightSampler(ctx.Settings.Seed, ctx.Settings.SeaLevel,
                    ctx.Settings.MaxHeight, preset,
                    ctx.Settings.Tuning.terrainRoughness),
      Preset(preset)
{
}

void UOverworldPipeline::GenerateColumn(int worldX, int worldZ)
{
  const int naturalY = HeightSampler.SurfaceYAt(worldX, worldZ);
  const int surfaceY =
      AdjustSurfaceYForSpawnIsland(worldX, worldZ, naturalY, Ctx.Settings);
  ColumnLayerRule rule;
  rule.surfaceBlock = Ctx.Grass;
  rule.subsurfaceBlock = Ctx.Dirt;
  rule.fillerBlock = Ctx.Stone;

  if (Preset == HeightPreset::Mountains &&
      HeightSampler.params().stoneSurfaceAboveY > 0 &&
      surfaceY >= HeightSampler.params().stoneSurfaceAboveY)
  {
    rule.surfaceBlock = Ctx.Stone;
    rule.subsurfaceBlock = Ctx.Stone;
  }

  FillTerrainColumn(Ctx, worldX, worldZ, surfaceY, rule);
  FillFluidColumn(Ctx, worldX, worldZ, surfaceY);
}

int UOverworldPipeline::SurfaceYAt(int worldX, int worldZ) const
{
  const int naturalY = HeightSampler.SurfaceYAt(worldX, worldZ);
  return AdjustSurfaceYForSpawnIsland(worldX, worldZ, naturalY, Ctx.Settings);
}

} // namespace cutum
