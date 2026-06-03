#ifndef CREATUREVISUALGLTF_H
#define CREATUREVISUALGLTF_H

#include "CreatureVisual.h"

namespace cutum {

class CreatureVisualGltf : public ICreatureVisual {
public:
 void UpdatePose(const Creature& creature, LocomotionState state, const CreatureDefinition& animDef,
                 float dt) override;
 void SubmitDraw(GeometryEngine& engine, const glm::mat4& viewProj) override;
};

} // namespace cutum

#endif
