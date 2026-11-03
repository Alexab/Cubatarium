#pragma once

#include "WorldGen/Pipelines/ComposableWorldGenerator.h"
#include "WorldGen/Sampling/ColumnSample.h"
#include <array>

namespace cutum
{

void RunTerrainStage(UComposableWorldGenerator &generator,
                     const ColumnSampleContext &sample, int world_x,
                     int world_z);

void RunPostTerrainStages(UComposableWorldGenerator &generator,
                          const ColumnSampleContext &sample, int world_x,
                          int world_z);

uint32_t WorldGenStageSkipBit(WorldGenStageId id);

void RunPostTerrainStagesExcluding(
    UComposableWorldGenerator &generator, const ColumnSampleContext &sample,
    int world_x, int world_z, uint32_t skip_stage_mask,
    std::array<double, WorldGenStageIdCount> *out_stage_ms = nullptr);

} // namespace cutum
