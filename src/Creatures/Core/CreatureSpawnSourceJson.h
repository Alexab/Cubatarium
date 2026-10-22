#ifndef CREATURESPAWNSOURCEJSON_H
#define CREATURESPAWNSOURCEJSON_H

#include "Creatures/Core/CreatureSpawnSource.h"

#include <nlohmann/json.hpp>

namespace cutum
{

inline void WriteCreatureSpawnSource(nlohmann::json &entity,
                                     CreatureSpawnSource source)
{
  entity["spawn_source"] = CreatureSpawnSourceToString(source);
}

inline CreatureSpawnSource
ReadCreatureSpawnSource(const nlohmann::json &entity)
{
  if (!entity.contains("spawn_source") ||
      !entity["spawn_source"].is_string())
  {
    return CreatureSpawnSource::LegacySave;
  }
  return CreatureSpawnSourceFromString(
      entity["spawn_source"].get<std::string>());
}

} // namespace cutum

#endif
