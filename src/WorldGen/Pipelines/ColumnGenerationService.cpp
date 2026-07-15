#include "WorldGen/Pipelines/ColumnGenerationService.h"
#include "WorldGen/Core/BlockWorldColumnWriter.h"
#include "WorldGen/Core/IUColumnWriter.h"
#include "WorldGen/Pipelines/WorldGenStageRunner.h"
#include "WorldGen/Stages/WorldGenStages.h"

namespace cutum
{

void UColumnGenerationService::GenerateColumn(
    UComposableWorldGenerator &generator, int world_x, int world_z)
{
  UBlockWorldColumnWriter writer(generator.GetContext().World,
                                 generator.GetContext().Registry);
  GenerateColumn(generator, writer, world_x, world_z);
}

void UColumnGenerationService::GenerateColumn(
    UComposableWorldGenerator &generator, IUColumnWriter &writer, int world_x,
    int world_z)
{
  WorldGenContext &ctx = generator.GetContext();
  if (&writer.GetBlockWorld() != &ctx.World)
  {
    return;
  }
  if (&writer.GetRegistry() != &ctx.Registry)
  {
    return;
  }

  const ComposableWorldGenConfig &config = generator.GetConfig();
  ctx.ResetColumnDirty(world_x, world_z);
  if (config.TerrainMode == ComposableTerrainMode::Flat)
  {
    FillFlatColumn(ctx, world_x, world_z);
    ctx.FlushColumnDirty();
    return;
  }
  if (config.TerrainMode == ComposableTerrainMode::LegacyHash)
  {
    FillLegacyHashColumn(ctx, world_x, world_z);
    ctx.FlushColumnDirty();
    return;
  }

  const ColumnSampleContext sample =
      generator.BuildColumnSample(world_x, world_z);
  RunTerrainStage(generator, sample, world_x, world_z);
  RunPostTerrainStages(generator, sample, world_x, world_z);
  ctx.FlushColumnDirty();
}

void UColumnGenerationService::GenerateColumnTerrainOnly(
    UComposableWorldGenerator &generator, IUColumnWriter &writer, int world_x,
    int world_z)
{
  WorldGenContext &ctx = generator.GetContext();
  if (&writer.GetBlockWorld() != &ctx.World || &writer.GetRegistry() != &ctx.Registry)
  {
    return;
  }
  const ComposableWorldGenConfig &config = generator.GetConfig();
  ctx.ResetColumnDirty(world_x, world_z);
  if (config.TerrainMode == ComposableTerrainMode::Flat)
  {
    FillFlatColumn(ctx, world_x, world_z);
    ctx.FlushColumnDirty();
    return;
  }
  if (config.TerrainMode == ComposableTerrainMode::LegacyHash)
  {
    FillLegacyHashColumn(ctx, world_x, world_z);
    ctx.FlushColumnDirty();
    return;
  }

  const ColumnSampleContext sample =
      generator.BuildColumnSample(world_x, world_z);
  RunTerrainStage(generator, sample, world_x, world_z);
  ctx.FlushColumnDirty();
}

void UColumnGenerationService::GenerateColumnPostTerrain(
    UComposableWorldGenerator &generator, IUColumnWriter &writer, int world_x,
    int world_z, uint32_t skip_stage_mask)
{
  WorldGenContext &ctx = generator.GetContext();
  if (&writer.GetBlockWorld() != &ctx.World || &writer.GetRegistry() != &ctx.Registry)
  {
    return;
  }
  const ComposableWorldGenConfig &config = generator.GetConfig();
  if (config.TerrainMode == ComposableTerrainMode::Flat ||
      config.TerrainMode == ComposableTerrainMode::LegacyHash)
  {
    return;
  }

  ctx.ResetColumnDirty(world_x, world_z);
  const ColumnSampleContext sample =
      generator.BuildColumnSample(world_x, world_z);
  RunPostTerrainStagesExcluding(generator, sample, world_x, world_z,
                                skip_stage_mask);
  ctx.FlushColumnDirty();
}

} // namespace cutum
