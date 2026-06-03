#ifndef CREATUREVISUALRIGID_H
#define CREATUREVISUALRIGID_H

#include "CreatureVisual.h"

namespace cutum {

class CreatureVisualRigid : public ICreatureVisual {
public:
 void UpdatePose(const Creature& creature, LocomotionState state, const CreatureDefinition& animDef,
                 float dt) override;
 void SubmitDraw(GeometryEngine& engine, const glm::mat4& viewProj) override;

private:
 float headYaw_{0.0f};
};

} // namespace cutum

#endif
