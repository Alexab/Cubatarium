#include "FlatPipeline.h"
#include "WorldGenStages.h"

namespace cutum {

FlatPipeline::FlatPipeline(WorldGenContext ctx)
 : IWorldGenPipeline(ctx)
{
}

void FlatPipeline::GenerateColumn(int worldX, int worldZ)
{
 FillFlatColumn(ctx_, worldX, worldZ);
}

int FlatPipeline::SurfaceYAt(int worldX, int worldZ) const
{
 (void)worldX;
 (void)worldZ;
 return ctx_.settings.flatSurfaceY;
}

LegacyHashPipeline::LegacyHashPipeline(WorldGenContext ctx)
 : IWorldGenPipeline(ctx)
{
}

void LegacyHashPipeline::GenerateColumn(int worldX, int worldZ)
{
 FillLegacyHashColumn(ctx_, worldX, worldZ);
}

int LegacyHashPipeline::SurfaceYAt(int worldX, int worldZ) const
{
 return LegacyHashSurfaceY(worldX, worldZ, ctx_.settings);
}

} // namespace cutum
