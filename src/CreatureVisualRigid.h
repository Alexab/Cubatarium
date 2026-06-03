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
 glm::vec3 bodyOrigin_{0.0f};
 glm::vec3 sizeBlocks_{0.8f, 1.6f, 0.8f};
};

} // namespace cutum

#endif
