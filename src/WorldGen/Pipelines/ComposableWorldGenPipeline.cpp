#include "WorldGen/Pipelines/ComposableWorldGenPipeline.h"
#include "WorldGen/Core/WorldGenPack.h"

namespace cutum
{

namespace
{

bool StageEnabled(const WorldGenPackPipeline &pipeline, bool generatorFlag,
                  bool pipelineFlag)
{
  if (!pipeline.Loaded)
  {
    return generatorFlag;
  }
  return generatorFlag && pipelineFlag;
}

} // namespace

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

} // namespace cutum
