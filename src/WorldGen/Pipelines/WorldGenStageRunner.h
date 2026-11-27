#pragma once

#include "WorldGen/Pipelines/ComposableWorldGenerator.h"
#include "WorldGen/Core/WorldGenWriteExtent.h"
#include "WorldGen/Sampling/ColumnSample.h"
#include <array>
#include <optional>

namespace cutum
{

void RunTerrainStage(UComposableWorldGenerator &generator,
                     const ColumnSampleContext &sample, int world_x,
                     int world_z);

void RunPostTerrainStages(UComposableWorldGenerator &generator,
                          const ColumnSampleContext &sample, int world_x,
                          int world_z);

uint32_t WorldGenStageSkipBit(WorldGenStageId id);

// Bounds the direct writes of the current pipeline configuration. Inspect
// under a stable content/strategy snapshot. No bound is reported for a
// strategy whose write domain has not been supplied.
std::optional<WorldGenWriteExtents>
GetWorldGenWriteExtents(const UComposableWorldGenerator &generator);

void RunPostTerrainStagesExcluding(
    UComposableWorldGenerator &generator, const ColumnSampleContext &sample,
    int world_x, int world_z, uint32_t skip_stage_mask,
    std::array<double, WorldGenStageIdCount> *out_stage_ms = nullptr);

} // namespace cutum
