#include "WorldGen/Stages/WorldGenStages.h"
#include "WorldGen/Sampling/AquiferSampler.h"
#include "WorldGen/Sampling/DensityFieldSampler.h"
#include "World/Core/BlockWorld.h"
#include "World/Math/FluidCellState.h"
#include <algorithm>

namespace cutum
{

void FillTerrainColumnFromDensity(WorldGenContext &ctx, int x, int z,
                                  int surfaceY, const ColumnLayerRule &rule,
                                  const UDensityFieldSampler &sampler,
                                  const UAquiferSampler *aquiferSampler)
{
  if (surfaceY < 1)
  {
    return;
  }
  const int bedrockTop = std::clamp(ctx.Settings.BedrockTopY, 0, surfaceY);
  const UDensityFieldSampler::Column column =
      sampler.PrepareColumn(x, z, surfaceY);
  int maxY = surfaceY;
  for (int y = 0; y <= ctx.Settings.MaxHeight; ++y)
  {
    if (sampler.SampleDensity(column, y) <= 0.0f)
    {
      if (aquiferSampler == nullptr || y >= surfaceY)
      {
        continue;
      }

      const AquiferSample aquifer = aquiferSampler->SampleAt(x, y, z);
      if (aquifer.Fluid == AquiferFluid::Barrier)
      {
        const BlockId barrier = ctx.Blocks.Stone != BLOCK_AIR
                                    ? ctx.Blocks.Stone
                                    : rule.fillerBlock;
        if (barrier != BLOCK_AIR)
        {
          const glm::ivec3 pos(x, y, z);
          ctx.World.SetBlock(pos, barrier);
          ctx.World.SetFluidState(pos, FluidCellState{});
          maxY = std::max(maxY, y);
        }
        continue;
      }

      BlockId fluid = BLOCK_AIR;
      FluidKind fluidKind = FluidKind::None;
      if (aquifer.Fluid == AquiferFluid::Water && ctx.Settings.FillWater &&
          ctx.Blocks.Water != BLOCK_AIR)
      {
        fluid = ctx.Blocks.Water;
        fluidKind = FluidKind::Water;
      }
      else if (aquifer.Fluid == AquiferFluid::Lava && ctx.Settings.FillLava &&
               ctx.Blocks.Lava != BLOCK_AIR)
      {
        fluid = ctx.Blocks.Lava;
        fluidKind = FluidKind::Lava;
      }
      if (fluid != BLOCK_AIR)
      {
        const glm::ivec3 pos(x, y, z);
        ctx.World.SetBlock(pos, fluid);
        ctx.World.SetFluidState(pos,
                                FluidCellState::Source().WithKind(fluidKind));
        maxY = std::max(maxY, y);
      }
      continue;
    }
    BlockId id = rule.fillerBlock;
    if (y <= bedrockTop)
    {
      id = ctx.Blocks.Bedrock;
    }
    else if (y < surfaceY - rule.dirtDepth - rule.stoneDepthBelowDirt)
    {
      id = rule.fillerBlock;
    }
    else if (y < surfaceY - rule.dirtDepth)
    {
      id = rule.fillerBlock;
    }
    else if (y < surfaceY)
    {
      id = rule.subsurfaceBlock;
    }
    else
    {
      id = rule.surfaceBlock;
    }
    ctx.World.SetBlock(glm::ivec3(x, y, z), id);
    maxY = std::max(maxY, y);
  }
  const int dirtyMax = std::max(maxY, ctx.Settings.SeaLevel);
  ctx.AccumulateDirtyColumn(0, dirtyMax);
}

} // namespace cutum
