#include "WorldGen/Pipelines/FlatPipeline.h"
#include "WorldGen/Stages/WorldGenStages.h"

namespace cutum
{

UFlatPipeline::UFlatPipeline(WorldGenContext ctx) : IWorldGenPipeline(ctx) {}

void UFlatPipeline::GenerateColumn(int worldX, int worldZ)
{
  FillFlatColumn(Ctx, worldX, worldZ);
}

int UFlatPipeline::SurfaceYAt(int worldX, int worldZ) const
{
  (void)worldX;
  (void)worldZ;
  return Ctx.Settings.FlatSurfaceY;
}

ULegacyHashPipeline::ULegacyHashPipeline(WorldGenContext ctx)
    : IWorldGenPipeline(ctx)
{
}

void ULegacyHashPipeline::GenerateColumn(int worldX, int worldZ)
{
  FillLegacyHashColumn(Ctx, worldX, worldZ);
}

int ULegacyHashPipeline::SurfaceYAt(int worldX, int worldZ) const
{
  const int naturalY = LegacyHashSurfaceY(worldX, worldZ, Ctx.Settings);
  return AdjustSurfaceYForSpawnIsland(worldX, worldZ, naturalY, Ctx.Settings);
}

} // namespace cutum
