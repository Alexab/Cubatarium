#include "WorldGen/Features/ObjectPlacementConstraints.h"
#include "World/Chunks/TerrainColumnUtil.h"
#include "World/Core/BlockWorld.h"
#include "World/Objects/ObjectUtil.h"
#include "WorldGen/Core/WorldGenPlacementTuning.h"

#include <cmath>

namespace cutum
{

namespace
{

bool IsGrassLandSurface(const WorldGenContext &ctx, int x, int z, int surface_y)
{
  if (ctx.Blocks.Grass == BLOCK_AIR)
  {
    return false;
  }
  if (ctx.World.GetBlock(glm::ivec3(x, surface_y, z)) != ctx.Blocks.Grass)
  {
    return false;
  }
  const int maxY = ComputeMaxScanY(surface_y, ctx.Settings.SeaLevel,
                                   ctx.Settings.MaxHeight);
  return HasOpenSurfaceAbove(ctx.World, ctx.Registry, x, surface_y, z, maxY);
}

bool IsNearSurfaceWater(const WorldGenContext &ctx, int x, int z, int surface_y,
                        int radius)
{
  if (ctx.Blocks.Water == BLOCK_AIR)
  {
    return false;
  }
  for (int dz = -radius; dz <= radius; ++dz)
  {
    for (int dx = -radius; dx <= radius; ++dx)
    {
      for (int dy = -1; dy <= 1; ++dy)
      {
        if (ctx.World.GetBlock(glm::ivec3(x + dx, surface_y + dy, z + dz)) ==
            ctx.Blocks.Water)
        {
          return true;
        }
      }
    }
  }
  return false;
}

bool TryWaterSurfaceAnchorAt(const WorldGenContext &ctx, int x, int z,
                             int &anchor_y)
{
  if (ctx.Blocks.Water == BLOCK_AIR)
  {
    return false;
  }
  const int max_y =
      std::min(ctx.Settings.MaxHeight - 1, ctx.Settings.SeaLevel + 1);
  for (int y = max_y; y >= 1; --y)
  {
    if (ctx.World.GetBlock(glm::ivec3(x, y, z)) != ctx.Blocks.Water)
    {
      continue;
    }
    if (!ctx.World.IsAir(glm::ivec3(x, y + 1, z)))
    {
      continue;
    }
    anchor_y = y + 1;
    return true;
  }
  return false;
}

bool FindWaterSurfaceAnchor(const WorldGenContext &ctx, int x, int z,
                            glm::ivec3 &anchor_out)
{
  for (int radius = 0; radius <= 3; ++radius)
  {
    for (int dz = -radius; dz <= radius; ++dz)
    {
      for (int dx = -radius; dx <= radius; ++dx)
      {
        if (radius > 0 && std::max(std::abs(dx), std::abs(dz)) != radius)
        {
          continue;
        }
        int anchor_y = 0;
        if (TryWaterSurfaceAnchorAt(ctx, x + dx, z + dz, anchor_y))
        {
          anchor_out = glm::ivec3(x + dx, anchor_y, z + dz);
          return true;
        }
      }
    }
  }
  return false;
}

} // namespace

bool SatisfiesSurfaceConstraint(const WorldGenContext &ctx,
                                const SurfaceConstraint &constraint,
                                const std::string &, int x, int z,
                                int surface_y)
{
  switch (constraint.Kind)
  {
  case SurfaceConstraintKind::Grass:
    return IsGrassLandSurface(ctx, x, z, surface_y);
  case SurfaceConstraintKind::NearWater:
    return IsNearSurfaceWater(ctx, x, z, surface_y, constraint.NearWaterRadius);
  case SurfaceConstraintKind::WaterSurface:
    return IsNearSurfaceWater(ctx, x, z, surface_y, constraint.NearWaterRadius);
  case SurfaceConstraintKind::AnyLand:
  default:
    return true;
  }
}

bool FindWaterSurfaceAnchorForPlacement(const WorldGenContext &ctx, int x,
                                        int z, glm::ivec3 &anchor_out)
{
  return FindWaterSurfaceAnchor(ctx, x, z, anchor_out);
}

float SampleStructureSurfaceGradient(const WorldGenContext &ctx, int x, int z,
                                     int surfaceY)
{
  const int maxScanY = ComputeMaxScanY(surfaceY, ctx.Settings.SeaLevel,
                                       ctx.Settings.MaxHeight);
  auto surfaceAt = [&](int wx, int wz) -> int {
    return FindTopSolidSurfaceY(ctx.World, ctx.Registry, wx, wz, maxScanY);
  };
  const int centerY = surfaceAt(x, z);
  if (centerY < 0)
  {
    return 999.0f;
  }
  float maxDelta = 0.0f;
  for (int dz = -4; dz <= 4; dz += 4)
  {
    for (int dx = -4; dx <= 4; dx += 4)
    {
      if (dx == 0 && dz == 0)
      {
        continue;
      }
      const int neighborY = surfaceAt(x + dx, z + dz);
      if (neighborY < 0)
      {
        continue;
      }
      maxDelta =
          std::max(maxDelta, static_cast<float>(std::abs(neighborY - centerY)));
    }
  }
  return maxDelta;
}

} // namespace cutum
