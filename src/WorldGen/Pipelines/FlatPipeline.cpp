#include "WorldGen/Pipelines/FlatPipeline.h"
#include "WorldGen/Stages/WorldGenStages.h"

namespace cutum
{

UFlatPipeline::UFlatPipeline(WorldGenContext ctx) : IWorldGenPipeline(ctx) {}

void UFlatPipeline::GenerateColumn(int worldX, int worldZ)
{
  FillFlatColumn(ctx_, worldX, worldZ);
}

int UFlatPipeline::SurfaceYAt(int worldX, int worldZ) const
{
  (void)worldX;
  (void)worldZ;
  return ctx_.Settings.flatSurfaceY;
}

ULegacyHashPipeline::ULegacyHashPipeline(WorldGenContext ctx)
    : IWorldGenPipeline(ctx)
{
}

void ULegacyHashPipeline::GenerateColumn(int worldX, int worldZ)
{
  FillLegacyHashColumn(ctx_, worldX, worldZ);
}

int ULegacyHashPipeline::SurfaceYAt(int worldX, int worldZ) const
{
  const int naturalY = LegacyHashSurfaceY(worldX, worldZ, ctx_.Settings);
  return AdjustSurfaceYForSpawnIsland(worldX, worldZ, naturalY, ctx_.Settings);
}

} // namespace cutum
