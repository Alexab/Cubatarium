#include "CreatureVisualGltf.h"
#include "Creature.h"
#include "CreatureDefinition.h"
#include <iostream>

namespace cutum {

void CreatureVisualGltf::UpdatePose(const Creature& /*creature*/, const CreatureLocomotionFacts& /*facts*/,
                                    const CreaturePoseParams& /*pose*/,
                                    const CreatureDefinition& /*animDef*/, float /*dt*/)
{
}

void CreatureVisualGltf::SubmitDraw(GeometryEngine& /*engine*/, const glm::mat4& /*viewProj*/)
{
 // glTF skeleton backend stub (phase D)
}

} // namespace cutum
