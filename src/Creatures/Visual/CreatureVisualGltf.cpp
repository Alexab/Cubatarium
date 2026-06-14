#include "Creatures/Visual/CreatureVisualGltf.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include <iostream>

namespace cutum
{

void UCreatureVisualGltf::UpdatePose(const UCreature & /*creature*/,
                                     const CreatureLocomotionFacts & /*facts*/,
                                     const CreaturePoseParams & /*pose*/,
                                     const CreatureDefinition & /*animDef*/,
                                     float /*dt*/)
{
}

void UCreatureVisualGltf::SubmitDraw(UGeometryEngine & /*engine*/,
                                     const glm::mat4 & /*viewProj*/)
{
  // glTF skeleton backend stub (phase D)
}

} // namespace cutum
