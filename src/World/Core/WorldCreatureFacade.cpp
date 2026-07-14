#include "World/Core/WorldCreatureFacade.h"

#include "World/Core/World.h"
#include "World/Environment/WorldEnvironment.h"

namespace cutum
{

bool UWorldCreatureFacade::SpawnCreatureByView(UWorld &world,
                                               const std::string &species_id)
{
  return world.Environment.SpawnCreatureByView(species_id);
}

bool UWorldCreatureFacade::CanSpawnCreatureByView(UWorld &world,
                                                const std::string &species_id)
{
  return world.Environment.CanSpawnCreatureByView(species_id);
}

std::string UWorldCreatureFacade::GetCreatureSpawnBlockedHint(
    UWorld &world, const std::string &species_id)
{
  return world.Environment.GetCreatureSpawnBlockedHint(species_id);
}

} // namespace cutum
