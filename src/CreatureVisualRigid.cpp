#include "CreatureVisualRigid.h"
#include "Creature.h"
#include "CreatureDefinition.h"

namespace cutum {

void CreatureVisualRigid::UpdatePose(const Creature& /*creature*/, LocomotionState state,
                                     const CreatureDefinition& /*animDef*/, float /*dt*/)
{
 switch (state) {
 case LocomotionState::Walk:
  headYaw_ = 0.2f;
  break;
 case LocomotionState::Fly:
  headYaw_ = 0.1f;
  break;
 default:
  headYaw_ = 0.0f;
  break;
 }
}

void CreatureVisualRigid::SubmitDraw(GeometryEngine& /*engine*/, const glm::mat4& /*viewProj*/)
{
 // Rigid voxel mesh draw (iteration B: pose state only; full mesh pass later)
}

} // namespace cutum
