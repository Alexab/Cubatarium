#include "WorldGen/Core/WorldGenContentReload.h"
#include "WorldGen/Core/WorldGenPack.h"
#include "WorldGen/Features/ObjectFeatureConfig.h"
#include <iostream>

namespace cutum
{

bool ReloadWorldGenContent()
{
  const std::string packId = UWorldGenPack::Get().Id;
  const bool packOk = UWorldGenPack::ReloadActive();
  const bool prefabOk =
      UObjectFeatureConfigStorage::LoadFromFile("content/object_features.json");
  if (packOk)
  {
    std::cout << "WorldGen: reloaded pack '" << packId << "'" << std::endl;
  }
  if (prefabOk)
  {
    std::cout << "WorldGen: reloaded object_features.json" << std::endl;
  }
  return packOk && prefabOk;
}

} // namespace cutum
