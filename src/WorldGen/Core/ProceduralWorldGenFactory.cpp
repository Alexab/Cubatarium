#include "WorldGen/Core/IUWorldGenPipeline.h"
#include "WorldGen/Core/WorldGeneratorDescriptor.h"
#include "WorldGen/Core/WorldGenPack.h"
#include <iostream>
#include <memory>

namespace cutum
{

std::unique_ptr<IUWorldGenPipeline>
UProceduralWorldGenFactory::Create(WorldGenContext ctx)
{
  ctx.ResolveBlockIds();

  std::string packId = ctx.Settings.WorldGenPackId;
  if (packId.empty())
  {
    if (const WorldGeneratorDescriptor *descriptor =
            UWorldGeneratorRegistry::Find(ctx.Settings.Generator))
    {
      packId = descriptor->PackId ? descriptor->PackId : "default";
    }
    else
    {
      packId = "default";
    }
  }
  if (!UWorldGenPack::LoadPackId(packId))
  {
    UWorldGenPack::LoadPackId("default");
  }
  if (UWorldGenPack::Get().BiomeBlendRadius >= 0.0f)
  {
    ctx.Settings.Tuning.biomeBlendRadius = UWorldGenPack::Get().BiomeBlendRadius;
  }

  auto pipeline = UWorldGeneratorRegistry::Create(ctx);
  std::cout << "WorldGen: created pipeline "
            << ProceduralGeneratorToString(ctx.Settings.Generator)
            << " (pack " << UWorldGenPack::Get().Id << ")" << std::endl;
  return pipeline;
}

} // namespace cutum
