#ifndef CREATUREDEFINITION_H
#define CREATUREDEFINITION_H

#include <string>
#include "CreatureBounds.h"
#include "CreatureCatalogTypes.h"
#include "LocomotionTypes.h"

namespace cutum {

struct CreatureDefinition {
 std::string id;
 std::string displayName;
 CreatureCatalogInfo catalog;
 CreatureRole role{CreatureRole::Unknown};
 CreatureBoundsProfile bounds;
 float eyeHeight{1.62f};
 CreatureLocomotionCapabilities locomotion;
 CreatureBehaviorParams behavior;
 CreatureVisualSpec visual;
};

} // namespace cutum

#endif
