#pragma once

#include "WorldGen/Pipelines/ComposableWorldGenerator.h"
#include "WorldGen/Sampling/ColumnSample.h"

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
  // Builds the column sample once, runs terrain, and returns the sample for
  // reuse by carve callbacks and post-terrain stages.
  static ColumnSampleContext
  GenerateColumnTerrainOnly(UComposableWorldGenerator &generator,
                            IUColumnWriter &writer, int world_x, int world_z);
  static void GenerateColumnPostTerrain(UComposableWorldGenerator &generator,
                                        IUColumnWriter &writer, int world_x,
                                        int world_z, uint32_t skip_stage_mask,
                                        const ColumnSampleContext &sample);
};

} // namespace cutum
