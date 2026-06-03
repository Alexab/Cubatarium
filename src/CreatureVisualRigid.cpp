#include "CreatureVisualRigid.h"
#include "Creature.h"
#include "CreatureBounds.h"
#include "CreatureDefinition.h"
#include "GeometryEngine.h"
#include <glm/gtc/matrix_transform.hpp>

namespace cutum {

void CreatureVisualRigid::UpdatePose(const Creature& creature, LocomotionState state,
                                     const CreatureDefinition& /*animDef*/, float /*dt*/)
{
 bodyOrigin_ = creature.GetBodyOrigin();
 sizeBlocks_ = creature.GetBounds().currentSizeBlocks;
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

void CreatureVisualRigid::SubmitDraw(GeometryEngine& engine, const glm::mat4& viewProj)
{
 const glm::vec3 center = BoundsCollisionCenter(bodyOrigin_, sizeBlocks_);
 glm::mat4 model = glm::translate(glm::mat4(1.0f), center);
 model = glm::scale(model, sizeBlocks_);
 engine.DrawBoxWireframe(viewProj * model, appearance_.wireframeColor);
}

} // namespace cutum
