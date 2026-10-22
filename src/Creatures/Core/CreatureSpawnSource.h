#ifndef CREATURESPAWNSOURCE_H
#define CREATURESPAWNSOURCE_H

#include <cstdint>
#include <string>

namespace cutum
{

/// Who requested creation. Persistence is reserved for world restoration;
/// LegacySave identifies entities written before source tracking existed.
enum class CreatureSpawnSource : uint8_t
{
  CreativeManual,
  SurvivalNatural,
  DebugCommand,
  Qa,
  Persistence,
  LegacySave,
};

inline bool CountsTowardNaturalPopulationLimit(CreatureSpawnSource source)
{
  // Saves written before source tracking cannot distinguish natural animals
  // from other entities. Count them conservatively so upgrading an old world
  // cannot silently overfill the natural-population budget.
  return source == CreatureSpawnSource::SurvivalNatural ||
         source == CreatureSpawnSource::LegacySave;
}

inline const char *CreatureSpawnSourceToString(CreatureSpawnSource source)
{
  switch (source)
  {
  case CreatureSpawnSource::CreativeManual:
    return "creative_manual";
  case CreatureSpawnSource::SurvivalNatural:
    return "survival_natural";
  case CreatureSpawnSource::DebugCommand:
    return "debug_command";
  case CreatureSpawnSource::Qa:
    return "qa";
  case CreatureSpawnSource::Persistence:
    return "persistence";
  case CreatureSpawnSource::LegacySave:
  default:
    return "legacy_save";
  }
}

inline CreatureSpawnSource
CreatureSpawnSourceFromString(const std::string &source)
{
  if (source == "creative_manual")
  {
    return CreatureSpawnSource::CreativeManual;
  }
  if (source == "survival_natural")
  {
    return CreatureSpawnSource::SurvivalNatural;
  }
  if (source == "debug_command")
  {
    return CreatureSpawnSource::DebugCommand;
  }
  if (source == "qa")
  {
    return CreatureSpawnSource::Qa;
  }
  if (source == "persistence")
  {
    return CreatureSpawnSource::Persistence;
  }
  return CreatureSpawnSource::LegacySave;
}

} // namespace cutum

#endif
