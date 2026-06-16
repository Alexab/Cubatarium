#include "WorldGen/Core/IWorldGenPipeline.h"
#include "WorldGen/Pipelines/FlatPipeline.h"
#include "WorldGen/Pipelines/OverworldBiomesPipeline.h"
#include "WorldGen/Pipelines/OverworldFullPipeline.h"
#include "WorldGen/Pipelines/OverworldPipeline.h"
#include <iostream>
#include <memory>

namespace cutum
{

std::unique_ptr<IWorldGenPipeline>
UProceduralWorldGenFactory::Create(WorldGenContext ctx)
{
  ctx.ResolveBlockIds();

  std::unique_ptr<IWorldGenPipeline> pipeline;
  switch (ctx.Settings.Generator)
  {
  case ProceduralGenerator::Flat:
    pipeline = std::make_unique<UFlatPipeline>(ctx);
    break;
  case ProceduralGenerator::Heightmap:
    pipeline = std::make_unique<ULegacyHashPipeline>(ctx);
    break;
  case ProceduralGenerator::Overworld:
    pipeline =
        std::make_unique<UOverworldPipeline>(ctx, HeightPreset::Overworld);
    break;
  case ProceduralGenerator::Hills:
    pipeline = std::make_unique<UOverworldPipeline>(ctx, HeightPreset::Hills);
    break;
  case ProceduralGenerator::Mountains:
    pipeline =
        std::make_unique<UOverworldPipeline>(ctx, HeightPreset::Mountains);
    break;
  case ProceduralGenerator::OverworldBiomes:
    pipeline = std::make_unique<UOverworldBiomesPipeline>(ctx);
    break;
  case ProceduralGenerator::OverworldFull:
    pipeline = std::make_unique<UOverworldFullPipeline>(ctx);
    break;
  default:
    std::cerr << "WorldGen: unknown Generator, using heightmap" << std::endl;
    pipeline = std::make_unique<ULegacyHashPipeline>(ctx);
    break;
  }

  std::cout << "WorldGen: created pipeline "
            << ProceduralGeneratorToString(ctx.Settings.Generator) << std::endl;
  return pipeline;
}

} // namespace cutum
