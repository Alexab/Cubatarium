#ifndef CREATUREDEFINITION_H
#define CREATUREDEFINITION_H

#include "CreatureBounds.h"
#include "CreatureCatalogTypes.h"
#include <string>

#include "LocomotionTypes.h"

namespace cutum
{

struct CreatureDefinition
{
  std::string id;
  std::string displayName;
  CreatureCatalogInfo catalog;
  CreatureRole role{CreatureRole::Unknown};
  CreatureBoundsProfile bounds;
  float eyeHeight{1.62f};
  CreatureLocomotionCapabilities locomotion;
  LocomotionArchetype locomotionArchetype{
      LocomotionArchetype::TerrestrialBiped};
  CreatureBehaviorParams behavior;
  CreatureVisualSpec visual;
};

} // namespace cutum

#endif
