#ifndef CREATUREVISUALSPRITE_H
#define CREATUREVISUALSPRITE_H

#include "Creatures/Visual/CreatureVisualBase.h"
#include <memory>

namespace cutum
{

class UCreatureVisualSprite : public UCreatureVisualBase
{
public:
  void UpdatePose(const UCreature &creature,
                  const CreatureLocomotionFacts &facts,
                  const CreaturePoseParams &pose,
                  const CreatureDefinition &animDef, float dt) override;
  void SubmitDraw(const CreatureDrawContext &ctx,
                  const glm::mat4 &viewProj) override;

private:
  glm::vec3 SizeBlocks{0.8f, 1.6f, 0.8f};
  std::string TextureStem{"body"};
  glm::vec4 EmissiveTint{1.f, 1.f, 1.f, 1.f};
};

std::unique_ptr<IUCreatureVisual> CreateCreatureVisualSprite();

} // namespace cutum

#endif
