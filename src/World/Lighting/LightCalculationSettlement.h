#pragma once

#include "App/Settings/RenderSettings.h"
#include "World/Core/BlockWorld.h"
#include "World/Streaming/ChunkRenderDemand.h"

namespace cutum
{
/// Rendering needs both the section's publication identity and current
/// calculation provenance. Own field revisions cannot prove neighbor inputs.
inline bool LightSettlementMatchesCurrentCalculation(
    const ChunkRenderDemandRecord *record, const UChunk *chunk,
    uint64_t world_epoch, const UBlockWorld &world,
    const std::shared_ptr<const BlockDefinitionCatalog> &catalog)
{
  return LightSettlementMatchesCurrentChunk(record, chunk, world_epoch) &&
         (world.GetLightingConfigurationIdentity() ==
              static_cast<uint64_t>(LightingMode::Flat) ||
          world.HasCompleteLightSamplingCalculation(chunk->GetCoord(),
                                                    catalog));
}
} // namespace cutum
