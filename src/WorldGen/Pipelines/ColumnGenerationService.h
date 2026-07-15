#pragma once

#include "WorldGen/Pipelines/ComposableWorldGenerator.h"

namespace cutum
{

class IUColumnWriter;

class UColumnGenerationService
{
public:
  static void GenerateColumn(UComposableWorldGenerator &generator, int world_x,
                             int world_z);
  static void GenerateColumn(UComposableWorldGenerator &generator,
                             IUColumnWriter &writer, int world_x, int world_z);
  static void GenerateColumnTerrainOnly(UComposableWorldGenerator &generator,
                                        IUColumnWriter &writer, int world_x,
                                        int world_z);
  static void GenerateColumnPostTerrain(UComposableWorldGenerator &generator,
                                        IUColumnWriter &writer, int world_x,
                                        int world_z,
                                        uint32_t skip_stage_mask);
};

} // namespace cutum
