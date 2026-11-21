#include "WorldGen/Core/IUWorldGenPipeline.h"
#include "WorldGen/Core/WorldGeneratorDescriptor.h"
#include "WorldGen/Core/WorldGenPack.h"
#include "WorldGen/Core/WorldGenContentPin.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include "WorldGen/Core/SettingsTextBudget.h"
#include <iostream>
#include <memory>

namespace cutum
{

std::unique_ptr<IUWorldGenPipeline>
UProceduralWorldGenFactory::Create(WorldGenContext ctx)
{
  ctx.ResolveBlockIds();

  // A Populate job has already captured its immutable catalog. Loading here
  // would publish another global version while Get() still reads the pin,
  // and put filesystem/JSON/image construction inside a generation worker.
  const auto *pinned=GetPinnedWorldGenContent();
  if (!pinned || !pinned->Pack)
  {
    USettingsTextBudget selector_budget(ExternalStringCapacityBytes(ctx.Settings.WorldGenPackId));
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
  }
  // Worldgen profile >= 3: the values from the New World form are
  // authoritative. Older profiles keep the historical behaviour where the pack
  // overwrote blend radius, cave depth and ravine settings.
  const bool userOwned = ctx.Settings.WorldGenProfileVersion >= 3;
  if (!userOwned && UWorldGenPack::Get().BiomeBlendRadius >= 0.0f)
  {
    ctx.Settings.Tuning.biomeBlendRadius = UWorldGenPack::Get().BiomeBlendRadius;
  }
  UWorldGenPack::ApplyPackCaveDefaults(ctx.Settings, userOwned);
  UWorldGenPack::ApplyPackRavineDefaults(ctx.Settings, userOwned);

  const auto generator_id=ctx.Settings.Generator;
  auto pipeline = UWorldGeneratorRegistry::Create(std::move(ctx));
  std::cout << "WorldGen: created pipeline "
            << ProceduralGeneratorToString(generator_id)
            << " (pack " << UWorldGenPack::Get().Id << ")" << std::endl;
  return pipeline;
}

} // namespace cutum
