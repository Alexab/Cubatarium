#ifndef CREATUREVISUALRIGID_H
#define CREATUREVISUALRIGID_H

#include "Pose/CreaturePoseParams.h"
#include "Creatures/Visual/CreatureVisual.h"
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
  float headYaw_{0.0f};
  std::unordered_map<std::string, CreaturePartPose> partPoses_;
  float bodyYaw_{-90.0f};
  float crouchUpperDrop_{0.0f};
  glm::vec3 BodyOrigin{0.0f};
  glm::vec3 sizeBlocks_{0.8f, 1.6f, 0.8f};
};

} // namespace cutum

#endif
