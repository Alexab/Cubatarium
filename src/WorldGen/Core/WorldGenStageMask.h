#pragma once

#include "WorldGen/Core/ProceduralSettings.h"
#include "WorldGen/Core/WorldGenStageId.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cutum
{

struct ComposableWorldGenConfig;
struct WorldGenPackPipeline;

struct WorldGenStageMask
{
  uint32_t Bits{0};

  bool IsEnabled(WorldGenStageId id) const;
  void Set(WorldGenStageId id, bool enabled);
};

ComposableWorldGenConfig ApplyPackPipelineMask(ComposableWorldGenConfig config);

WorldGenStageMask BuildWorldGenStageMask(
    const ComposableWorldGenConfig &generator_config,
    const ProceduralSettings &settings,
    const WorldGenPackPipeline &pack_pipeline);

std::optional<WorldGenStageId> WorldGenStageIdFromPipelineString(
    const std::string &stage);
std::vector<WorldGenStageId> DefaultPostTerrainStageOrder();

} // namespace cutum
