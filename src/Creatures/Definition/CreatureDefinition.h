#ifndef CREATUREDEFINITION_H
#define CREATUREDEFINITION_H

#include "Creatures/Core/CreatureBounds.h"
#include "Creatures/Core/CreatureCatalogTypes.h"
#include "Creatures/Influence/InfluenceTypes.h"
#include "Creatures/Stats/CreatureAttributes.h"
#include "Creatures/Stats/CreatureVitals.h"
#include <string>

#include "Creatures/Locomotion/LocomotionTypes.h"

namespace cutum
{

struct CreatureBareHandSpec
{
  bool hasOverride{false};
  int fleshyDamage{0};
  float fullPunchInterval{0.5f};
};

struct CreatureStatsSpec
{
  bool hasVitalsOverride{false};
  bool hasAttributesOverride{false};
  bool hasArmorGroupsOverride{false};
  /// Maxima / armor / fatal wounds template; currents filled at spawn.
  CreatureVitals vitalsTemplate{};
  CreatureAttributes attributes{};
  ArmorGroups armorGroups{ArmorGroups::DefaultFleshy()};
  CreatureBareHandSpec bareHand{};
  bool needsTick{false};
};

struct CreatureDefinition
{
  std::string Id;
  std::string displayName;
  CreatureCatalogInfo catalog;
  CreatureRole role{CreatureRole::Unknown};
  CreatureBoundsProfile bounds;
  float eyeHeight{1.62f};
  CreatureLocomotionCapabilities locomotion;
  LocomotionArchetype locomotionArchetype{
      LocomotionArchetype::TerrestrialBiped};
  CreatureHabitat habitat{CreatureHabitat::Terrestrial};
  CreatureBehaviorParams behavior;
  CreatureVisualSpec visual;
  CreatureStatsSpec stats;
};

} // namespace cutum

#endif
