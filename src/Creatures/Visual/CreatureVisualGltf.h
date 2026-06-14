#ifndef CREATUREVISUALGLTF_H
#define CREATUREVISUALGLTF_H

#include "Creatures/Visual/CreatureVisual.h"

namespace cutum
{

class UCreatureVisualGltf : public ICreatureVisual
{
public:
  void UpdatePose(const UCreature &creature,
                  const CreatureLocomotionFacts &facts,
                  const CreaturePoseParams &pose,
                  const CreatureDefinition &animDef, float dt) override;
  void SubmitDraw(UGeometryEngine &engine, const glm::mat4 &viewProj) override;
};

} // namespace cutum

#endif
