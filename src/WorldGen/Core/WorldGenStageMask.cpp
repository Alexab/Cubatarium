#include "WorldGen/Core/WorldGenStageMask.h"
#include "WorldGen/Core/WorldGenPack.h"
#include "WorldGen/Pipelines/ComposableWorldGenerator.h"
#include <algorithm>
#include <iostream>
#include <optional>
#include <vector>

namespace cutum
{

namespace
{

uint32_t StageBit(WorldGenStageId id)
{
  return 1u << static_cast<uint32_t>(id);
}

bool StageEnabled(const WorldGenPackPipeline &pipeline, bool generator_flag,
                  bool pipeline_flag)
{
  if (!pipeline.Loaded)
  {
    return generator_flag;
  }
  return generator_flag && pipeline_flag;
}

} // namespace

bool WorldGenStageMask::IsEnabled(WorldGenStageId id) const
{
  return (Bits & StageBit(id)) != 0;
}

void WorldGenStageMask::Set(WorldGenStageId id, bool enabled)
{
  const uint32_t bit = StageBit(id);
  if (enabled)
  {
    Bits |= bit;
  }
  else
  {
    Bits &= ~bit;
  }
}

ComposableWorldGenConfig ApplyPackPipelineMask(ComposableWorldGenConfig config)
{
  const WorldGenPackPipeline &pipeline = UWorldGenPack::Get().Pipeline;
  if (!pipeline.Loaded)
  {
    return config;
  }
  config.Fluids = StageEnabled(pipeline, config.Fluids, pipeline.Fluids);
  config.Ravines = StageEnabled(pipeline, config.Ravines, pipeline.Ravines);
  config.Ores = StageEnabled(pipeline, config.Ores, pipeline.Ores);
  config.Caves = StageEnabled(pipeline, config.Caves, pipeline.Caves);
  config.Vegetation =
      StageEnabled(pipeline, config.Vegetation, pipeline.Vegetation);
  config.GroundCover =
      StageEnabled(pipeline, config.GroundCover, pipeline.GroundCover);
  config.Decoration =
      StageEnabled(pipeline, config.Decoration, pipeline.Decoration);
  config.Structures =
      StageEnabled(pipeline, config.Structures, pipeline.Structures);
  config.LavaPools =
      StageEnabled(pipeline, config.LavaPools, pipeline.LavaPools);
  config.FirePatch =
      StageEnabled(pipeline, config.FirePatch, pipeline.FirePatch);
  return config;
}

WorldGenStageMask BuildWorldGenStageMask(
    const ComposableWorldGenConfig &generator_config,
    const ProceduralSettings &settings,
    const WorldGenPackPipeline &pack_pipeline)
{
  WorldGenStageMask mask;
  mask.Set(WorldGenStageId::Terrain, true);

  const bool ravines = StageEnabled(pack_pipeline, generator_config.Ravines,
                                    pack_pipeline.Ravines) &&
                       settings.Ravines.enabled;
  mask.Set(WorldGenStageId::Ravines, ravines);

  const bool caves = StageEnabled(pack_pipeline, generator_config.Caves,
                                  pack_pipeline.Caves) &&
                     settings.EnableCaves;
  mask.Set(WorldGenStageId::Caves, caves);

  const bool fluids = StageEnabled(pack_pipeline, generator_config.Fluids,
                                   pack_pipeline.Fluids);
  mask.Set(WorldGenStageId::Fluids, fluids);

  const bool ores = StageEnabled(pack_pipeline, generator_config.Ores,
                                 pack_pipeline.Ores) &&
                    settings.EnableOres;
  mask.Set(WorldGenStageId::Ores, ores);

  const bool vegetation =
      StageEnabled(pack_pipeline, generator_config.Vegetation,
                   pack_pipeline.Vegetation) &&
      settings.EnableTrees;
  mask.Set(WorldGenStageId::Vegetation, vegetation);

  const bool ground_cover =
      StageEnabled(pack_pipeline, generator_config.GroundCover,
                   pack_pipeline.GroundCover) &&
      settings.EnableGroundCover;
  mask.Set(WorldGenStageId::GroundCover, ground_cover);

  const bool decoration = StageEnabled(pack_pipeline, generator_config.Decoration,
                                       pack_pipeline.Decoration);
  mask.Set(WorldGenStageId::Decoration, decoration);

  const bool structures = StageEnabled(pack_pipeline, generator_config.Structures,
                                       pack_pipeline.Structures);
  mask.Set(WorldGenStageId::Structures, structures);

  const bool lava_pools =
      StageEnabled(pack_pipeline, generator_config.LavaPools,
                   pack_pipeline.LavaPools);
  mask.Set(WorldGenStageId::LavaPools, lava_pools);

  const bool fire_patch =
      StageEnabled(pack_pipeline, generator_config.FirePatch,
                   pack_pipeline.FirePatch);
  mask.Set(WorldGenStageId::FirePatch, fire_patch);

  return mask;
}

std::optional<WorldGenStageId> WorldGenStageIdFromPipelineString(
    const std::string &stage)
{
  if (stage == "terrain")
  {
    return std::nullopt;
  }
  if (stage == "ravines")
  {
    return WorldGenStageId::Ravines;
  }
  if (stage == "caves")
  {
    return WorldGenStageId::Caves;
  }
  if (stage == "fluids")
  {
    return WorldGenStageId::Fluids;
  }
  if (stage == "ores")
  {
    return WorldGenStageId::Ores;
  }
  if (stage == "vegetation")
  {
    return WorldGenStageId::Vegetation;
  }
  if (stage == "ground_cover")
  {
    return WorldGenStageId::GroundCover;
  }
  if (stage == "decoration")
  {
    return WorldGenStageId::Decoration;
  }
  if (stage == "structures")
  {
    return WorldGenStageId::Structures;
  }
  if (stage == "lava_pools")
  {
    return WorldGenStageId::LavaPools;
  }
  if (stage == "fire_patch")
  {
    return WorldGenStageId::FirePatch;
  }
  std::cerr << "WorldGen: unknown pipeline stage '" << stage << "'" << std::endl;
  return std::nullopt;
}

std::vector<WorldGenStageId> DefaultPostTerrainStageOrder()
{
  return {WorldGenStageId::Ravines,     WorldGenStageId::Caves,
          WorldGenStageId::Fluids,      WorldGenStageId::Ores,
          WorldGenStageId::Vegetation,  WorldGenStageId::GroundCover,
          WorldGenStageId::Decoration,  WorldGenStageId::Structures,
          WorldGenStageId::LavaPools,   WorldGenStageId::FirePatch};
}

} // namespace cutum
