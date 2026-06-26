#include "WorldGen/Core/WorldGenContext.h"
#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"
#include <algorithm>
#include <iostream>

namespace cutum
{

WorldGenContext::WorldGenContext(UBlockWorld &world, UBlockRegistry &registry,
                                 ProceduralSettings settings,
                                 UPrefabLibrary *prefabs)
    : World(world), Registry(registry), Settings(std::move(settings)),
      Prefabs(prefabs)
{
}

void WorldGenContext::ResolveBlockIds()
{
  Blocks.Resolve(Registry, WorldgenOwnerPackId);

  if (Settings.FillWater && Blocks.Water == BLOCK_AIR)
  {
    std::cerr << "WorldGen: block type 'water' not loaded — fill_water will "
                 "have no effect"
              << std::endl;
  }
}

ColumnWriteContext WorldGenContext::GetWriteContext()
{
  return ColumnWriteContext{World, Registry, Blocks, OnColumnMeshDirty};
}

void WorldGenContext::ResetColumnDirty(int world_x, int world_z)
{
  ColumnDirtyActive = true;
  ColumnDirtyWorldX = world_x;
  ColumnDirtyWorldZ = world_z;
  ColumnDirtyMinY = 0;
  ColumnDirtyMaxY = -1;
}

void WorldGenContext::AccumulateDirtyColumn(int min_y, int max_y)
{
  if (!ColumnDirtyActive)
  {
    return;
  }
  ColumnDirtyMinY = std::min(ColumnDirtyMinY, min_y);
  ColumnDirtyMaxY = std::max(ColumnDirtyMaxY, max_y);
}

void WorldGenContext::FlushColumnDirty()
{
  if (ColumnDirtyActive && ColumnDirtyMaxY >= ColumnDirtyMinY)
  {
    MarkDirtyColumn(ColumnDirtyWorldX, ColumnDirtyWorldZ, ColumnDirtyMinY,
                    ColumnDirtyMaxY);
  }
  ColumnDirtyActive = false;
}

void WorldGenContext::MarkDirtyColumn(int world_x, int world_z, int min_y,
                                      int max_y) const
{
  if (!OnColumnMeshDirty)
  {
    return;
  }
  OnColumnMeshDirty(world_x, world_z, min_y, max_y);
}

} // namespace cutum
