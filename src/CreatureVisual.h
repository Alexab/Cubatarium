#ifndef CREATUREVISUAL_H
#define CREATUREVISUAL_H

#include <glm/glm.hpp>
#include "LocomotionTypes.h"

namespace cutum {

class Creature;
class CreatureDefinition;
class GeometryEngine;

class ICreatureVisual {
public:
 virtual ~ICreatureVisual() = default;
 virtual void UpdatePose(const Creature& creature, LocomotionState state,
                         const CreatureDefinition& animDef, float dt) = 0;
 virtual void SubmitDraw(GeometryEngine& engine, const glm::mat4& viewProj) = 0;
};

} // namespace cutum

#endif
