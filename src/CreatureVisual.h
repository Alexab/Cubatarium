#ifndef CREATUREVISUAL_H
#define CREATUREVISUAL_H

#include <glm/glm.hpp>
#include "CreatureCatalogTypes.h"
#include "CreatureLocomotionFacts.h"
#include "CreaturePoseParams.h"
#include "LocomotionTypes.h"

namespace cutum {

class Creature;
struct CreatureDefinition;
class UGeometryEngine;

class ICreatureVisual {
public:
 virtual ~ICreatureVisual() = default;
 virtual void UpdatePose(const Creature& creature, const CreatureLocomotionFacts& facts,
                         const CreaturePoseParams& pose, const CreatureDefinition& animDef,
                         float dt) = 0;
 virtual void SetAppearance(const ResolvedCreatureAppearance& appearance) {
  appearance_ = appearance;
 }
 virtual void SubmitDraw(UGeometryEngine& engine, const glm::mat4& viewProj) = 0;

protected:
 ResolvedCreatureAppearance appearance_{};
};

} // namespace cutum

#endif
