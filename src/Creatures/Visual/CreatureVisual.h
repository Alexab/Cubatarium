#ifndef CREATUREVISUAL_H
#define CREATUREVISUAL_H

#include "Creatures/Core/CreatureCatalogTypes.h"
#include "Creatures/Locomotion/CreatureLocomotionFacts.h"
#include "Pose/CreaturePoseParams.h"
#include "Creatures/Locomotion/LocomotionTypes.h"
#include <glm/glm.hpp>

namespace cutum
{

class UCreature;
struct CreatureDefinition;
class UGeometryEngine;

class ICreatureVisual
{
public:
  virtual ~ICreatureVisual() = default;
  virtual void UpdatePose(const UCreature &creature,
                          const CreatureLocomotionFacts &facts,
                          const CreaturePoseParams &pose,
                          const CreatureDefinition &animDef, float dt) = 0;
  virtual void SetAppearance(const ResolvedCreatureAppearance &appearance)
  {
    appearance_ = appearance;
  }
  virtual void SubmitDraw(UGeometryEngine &engine,
                          const glm::mat4 &viewProj) = 0;

protected:
  ResolvedCreatureAppearance appearance_{};
};

} // namespace cutum

#endif
