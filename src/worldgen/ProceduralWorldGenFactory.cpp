#include "IWorldGenPipeline.h"
#include "FlatPipeline.h"
#include "OverworldPipeline.h"
#include "OverworldBiomesPipeline.h"
#include "OverworldFullPipeline.h"
#include <iostream>
#include <memory>

namespace cutum {

std::unique_ptr<IWorldGenPipeline> UProceduralWorldGenFactory::Create(WorldGenContext ctx)
{
 ctx.ResolveBlockIds();

 std::unique_ptr<IWorldGenPipeline> pipeline;
 switch (ctx.Settings.generator) {
 case ProceduralGenerator::Flat:
  pipeline = std::make_unique<UFlatPipeline>(ctx);
  break;
 case ProceduralGenerator::Heightmap:
  pipeline = std::make_unique<ULegacyHashPipeline>(ctx);
  break;
 case ProceduralGenerator::Overworld:
  pipeline = std::make_unique<UOverworldPipeline>(ctx, HeightPreset::Overworld);
  break;
 case ProceduralGenerator::Hills:
  pipeline = std::make_unique<UOverworldPipeline>(ctx, HeightPreset::Hills);
  break;
 case ProceduralGenerator::Mountains:
  pipeline = std::make_unique<UOverworldPipeline>(ctx, HeightPreset::Mountains);
  break;
 case ProceduralGenerator::OverworldBiomes:
  pipeline = std::make_unique<UOverworldBiomesPipeline>(ctx);
  break;
 case ProceduralGenerator::OverworldFull:
  pipeline = std::make_unique<UOverworldFullPipeline>(ctx);
  break;
 default:
  std::cerr << "WorldGen: unknown generator, using heightmap" << std::endl;
  pipeline = std::make_unique<ULegacyHashPipeline>(ctx);
  break;
 }

 std::cout << "WorldGen: created pipeline " << ProceduralGeneratorToString(ctx.Settings.generator)
           << std::endl;
 return pipeline;
}

} // namespace cutum
