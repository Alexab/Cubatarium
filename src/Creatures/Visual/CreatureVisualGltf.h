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

private:
  glm::vec3 BodyOrigin{0.f};
  glm::vec3 SizeBlocks{0.6f, 1.8f, 0.6f};
};

} // namespace cutum

#endif
