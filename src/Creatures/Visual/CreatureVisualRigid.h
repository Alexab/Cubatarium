#ifndef CREATUREVISUALRIGID_H
#define CREATUREVISUALRIGID_H

#include "Creatures/Visual/CreatureVisualBase.h"
#include "Pose/CreaturePoseParams.h"
#include <unordered_map>

namespace cutum
{

class UCreatureVisualRigid : public UCreatureVisualBase
{
public:
  void UpdatePose(const UCreature &creature,
                  const CreatureLocomotionFacts &facts,
                  const CreaturePoseParams &pose,
                  const CreatureDefinition &animDef, float dt) override;
  void SubmitDraw(const CreatureDrawContext &ctx, const glm::mat4 &viewProj) override;

private:
  std::unordered_map<std::string, CreaturePartPose> PartPoses;
  float CrouchUpperDrop{0.0f};
  glm::vec3 SizeBlocks{0.8f, 1.6f, 0.8f};
};

} // namespace cutum

#endif
