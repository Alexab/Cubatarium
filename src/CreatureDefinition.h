#ifndef CREATUREDEFINITION_H
#define CREATUREDEFINITION_H

#include <string>
#include "CreatureBounds.h"
#include "LocomotionTypes.h"

namespace cutum {

struct CreatureDefinition {
 std::string id;
 CreatureBoundsProfile bounds;
 float eyeHeight{1.62f};
 CreatureLocomotionCapabilities locomotion;
 std::string visualBackend{"rigid_voxels"};
};

} // namespace cutum

#endif
