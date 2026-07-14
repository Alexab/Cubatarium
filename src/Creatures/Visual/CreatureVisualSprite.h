#ifndef CREATUREVISUALSPRITE_H
#define CREATUREVISUALSPRITE_H

#include "Creatures/Visual/CreatureVisual.h"
#include <memory>

namespace cutum
{

class UCreatureVisualSprite : public IUCreatureVisual
{
public:
  void UpdatePose(const UCreature &creature,
                  const CreatureLocomotionFacts &facts,
                  const CreaturePoseParams &pose,
                  const CreatureDefinition &animDef, float dt) override;
  void SubmitDraw(UGeometryEngine &engine, const glm::mat4 &viewProj) override;

private:
  glm::vec3 BodyOrigin{0.0f};
  glm::vec3 SizeBlocks{0.8f, 1.6f, 0.8f};
  std::string SpeciesId;
  std::string TextureStem{"body"};
  glm::vec4 EmissiveTint{1.f, 1.f, 1.f, 1.f};
};

std::unique_ptr<IUCreatureVisual> CreateCreatureVisualSprite();

} // namespace cutum

#endif
