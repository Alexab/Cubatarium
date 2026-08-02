#ifndef CREATURE_STATS_JSON_H
#define CREATURE_STATS_JSON_H

#include "Creatures/Stats/CreatureAttributes.h"
#include "Creatures/Stats/CreatureVitals.h"
#include <nlohmann/json.hpp>

namespace cutum
{

struct CreatureStatsJson
{
  static void Write(nlohmann::json &out, const CreatureVitals &vitals,
                    const CreatureAttributes &attrs);
  /// Returns true if any vitals/attributes keys were present.
  static bool Read(const nlohmann::json &in, CreatureVitals &vitals,
                   CreatureAttributes &attrs);

  static void WriteVitalsTemplate(nlohmann::json &out,
                                  const CreatureVitals &vitals);
  static void WriteAttributes(nlohmann::json &out,
                              const CreatureAttributes &attrs);
  static void ReadVitalsTemplate(const nlohmann::json &in,
                                 CreatureVitals &vitals);
  static void ReadAttributes(const nlohmann::json &in,
                             CreatureAttributes &attrs);
};

} // namespace cutum

#endif
