#ifndef CREATURE_STATS_DEFAULTS_H
#define CREATURE_STATS_DEFAULTS_H

#include "Creatures/Core/CreatureCatalogTypes.h"
#include "Creatures/Locomotion/LocomotionTypes.h"
#include "Creatures/Stats/CreatureAttributes.h"
#include "Creatures/Stats/CreatureVitals.h"
#include <string>
#include <vector>

namespace cutum
{

struct CreatureStatsTemplate
{
  CreatureVitals vitals;
  CreatureAttributes attributes;
  bool needsTick{false};
};

struct CreatureStatsDefaults
{
  static CreatureStatsTemplate For(CreatureRole role,
                                   const std::vector<std::string> &tags,
                                   CreatureHabitat habitat,
                                   const std::string &speciesId = {});
};

} // namespace cutum

#endif
