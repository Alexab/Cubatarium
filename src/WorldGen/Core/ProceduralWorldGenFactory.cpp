#include "WorldGen/Core/IWorldGenPipeline.h"
#include "WorldGen/Core/WorldGeneratorDescriptor.h"
#include <iostream>
#include <memory>

namespace cutum
{

std::unique_ptr<IWorldGenPipeline>
UProceduralWorldGenFactory::Create(WorldGenContext ctx)
{
  ctx.ResolveBlockIds();

  auto pipeline = UWorldGeneratorRegistry::Create(ctx);
  std::cout << "WorldGen: created pipeline "
            << ProceduralGeneratorToString(ctx.Settings.Generator) << std::endl;
  return pipeline;
}

} // namespace cutum
