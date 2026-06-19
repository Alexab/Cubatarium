#ifndef CREATUREVISUALRIGID_H
#define CREATUREVISUALRIGID_H

#include "Creatures/Visual/CreatureVisual.h"
#include "Pose/CreaturePoseParams.h"
#include <unordered_map>

namespace cutum
{

class UCreatureVisualRigid : public ICreatureVisual
{
public:
  void UpdatePose(const UCreature &creature,
                  const CreatureLocomotionFacts &facts,
                  const CreaturePoseParams &pose,
                  const CreatureDefinition &animDef, float dt) override;
  void SubmitDraw(UGeometryEngine &engine, const glm::mat4 &viewProj) override;

private:
  std::unordered_map<std::string, CreaturePartPose> PartPoses;
  float BodyYaw{-90.0f};
  float CrouchUpperDrop{0.0f};
  glm::vec3 BodyOrigin{0.0f};
  glm::vec3 SizeBlocks{0.8f, 1.6f, 0.8f};
};

} // namespace cutum

#endif
