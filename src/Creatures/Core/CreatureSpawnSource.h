#ifndef CREATURESPAWNSOURCE_H
#define CREATURESPAWNSOURCE_H

#include <cstdint>
#include <string>

namespace cutum
{

/// Who requested creation. LegacySave identifies entities written before
/// source tracking existed (a stray "persistence" string from dead old code
/// also reads back as LegacySave).
enum class CreatureSpawnSource : uint8_t
{
  CreativeManual,
  NaturalPopulation,
  DebugCommand,
  Qa,
  LegacySave,
};

inline bool CountsTowardNaturalPopulationLimit(CreatureSpawnSource source)
{
  // Saves written before source tracking cannot distinguish natural animals
  // from other entities. Count them conservatively so upgrading an old world
  // cannot silently overfill the natural-population budget.
  return source == CreatureSpawnSource::NaturalPopulation ||
         source == CreatureSpawnSource::LegacySave;
}

inline const char *CreatureSpawnSourceToString(CreatureSpawnSource source)
{
  switch (source)
  {
  case CreatureSpawnSource::CreativeManual:
    return "creative_manual";
  case CreatureSpawnSource::NaturalPopulation:
    return "natural_population";
  case CreatureSpawnSource::DebugCommand:
    return "debug_command";
  case CreatureSpawnSource::Qa:
    return "qa";
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
  if (source == "natural_population" || source == "survival_natural")
  {
    return CreatureSpawnSource::NaturalPopulation;
  }
  if (source == "debug_command")
  {
    return CreatureSpawnSource::DebugCommand;
  }
  if (source == "qa")
  {
    return CreatureSpawnSource::Qa;
  }
  return CreatureSpawnSource::LegacySave;
}

} // namespace cutum

#endif
