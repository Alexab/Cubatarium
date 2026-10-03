#ifndef PLAYERCONTROLLER_H
#define PLAYERCONTROLLER_H

#include "Creatures/Locomotion/CreatureLocomotionController.h"
#include "Creatures/Locomotion/LocomotionTypes.h"

namespace cutum
{

/// Historical alias: camera / player movement uses UCreatureLocomotionController.
using PlayerController = UCreatureLocomotionController;
using PlayerInput = CreatureInput;
using PlayerMovementMode = CreatureMovementMode;

} // namespace cutum

#endif
