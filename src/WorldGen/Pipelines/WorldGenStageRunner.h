#pragma once

#include "WorldGen/Pipelines/ComposableWorldGenerator.h"
#include "WorldGen/Sampling/ColumnSample.h"

namespace cutum
{

void RunTerrainStage(UComposableWorldGenerator &generator,
                     const ColumnSampleContext &sample, int world_x,
                     int world_z);

void RunPostTerrainStages(UComposableWorldGenerator &generator,
                          const ColumnSampleContext &sample, int world_x,
                          int world_z);

} // namespace cutum
