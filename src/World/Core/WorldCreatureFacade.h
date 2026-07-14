#pragma once

#include <string>

namespace cutum
{

class UWorld;

class UWorldCreatureFacade
{
public:
  static bool SpawnCreatureByView(UWorld &world, const std::string &species_id);
  static bool CanSpawnCreatureByView(UWorld &world,
                                     const std::string &species_id);
  static std::string GetCreatureSpawnBlockedHint(UWorld &world,
                                                 const std::string &species_id);
};

} // namespace cutum
