#include "IWorldGenPipeline.h"
#include "FlatPipeline.h"
#include "OverworldPipeline.h"
#include "OverworldBiomesPipeline.h"
#include "OverworldFullPipeline.h"
#include <iostream>
#include <memory>

namespace cutum {

std::unique_ptr<IWorldGenPipeline> ProceduralWorldGenFactory::Create(WorldGenContext ctx)
{
 ctx.ResolveBlockIds();

 std::unique_ptr<IWorldGenPipeline> pipeline;
 switch (ctx.settings.generator) {
 case ProceduralGenerator::Flat:
  pipeline = std::make_unique<FlatPipeline>(ctx);
  break;
 case ProceduralGenerator::Heightmap:
  pipeline = std::make_unique<LegacyHashPipeline>(ctx);
  break;
 case ProceduralGenerator::Overworld:
  pipeline = std::make_unique<OverworldPipeline>(ctx, HeightPreset::Overworld);
  break;
 case ProceduralGenerator::Hills:
  pipeline = std::make_unique<OverworldPipeline>(ctx, HeightPreset::Hills);
  break;
 case ProceduralGenerator::Mountains:
  pipeline = std::make_unique<OverworldPipeline>(ctx, HeightPreset::Mountains);
  break;
 case ProceduralGenerator::OverworldBiomes:
  pipeline = std::make_unique<OverworldBiomesPipeline>(ctx);
  break;
 case ProceduralGenerator::OverworldFull:
  pipeline = std::make_unique<OverworldFullPipeline>(ctx);
  break;
 default:
  std::cerr << "WorldGen: unknown generator, using heightmap" << std::endl;
  pipeline = std::make_unique<LegacyHashPipeline>(ctx);
  break;
 }

 std::cout << "WorldGen: created pipeline " << ProceduralGeneratorToString(ctx.settings.generator)
           << std::endl;
 return pipeline;
}

} // namespace cutum
