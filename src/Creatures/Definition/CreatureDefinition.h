#ifndef CREATUREDEFINITION_H
#define CREATUREDEFINITION_H

#include "Creatures/Core/CreatureBounds.h"
#include "Creatures/Core/CreatureCatalogTypes.h"
#include <string>

#include "Creatures/Locomotion/LocomotionTypes.h"

namespace cutum
{

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
  CreatureBehaviorParams behavior;
  CreatureVisualSpec visual;
};

} // namespace cutum

#endif
