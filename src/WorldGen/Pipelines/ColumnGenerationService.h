#pragma once

#include "WorldGen/Pipelines/ComposableWorldGenerator.h"
#include "WorldGen/Sampling/ColumnSample.h"

namespace cutum
{

class IUColumnWriter;

/// Shared column entry for sync streaming and async chunk populate.
/// Sync: GenerateColumn (sample+terrain+post in one call).
/// Async Populate: GenerateColumnTerrainOnly → chunk carve → GenerateColumnPostTerrain
/// → IntraChunkSeal (same ColumnGenerationService + optional chunk phases).
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
                            IUColumnWriter &writer, int world_x, int world_z,
                            double *out_sample_ms = nullptr,
                            double *out_fill_ms = nullptr);
  static void GenerateColumnPostTerrain(UComposableWorldGenerator &generator,
                                        IUColumnWriter &writer, int world_x,
                                        int world_z, uint32_t skip_stage_mask,
                                        const ColumnSampleContext &sample);
};

} // namespace cutum
