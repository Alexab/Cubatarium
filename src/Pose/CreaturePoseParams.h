#ifndef CREATUREPOSEPARAMS_H
#define CREATUREPOSEPARAMS_H

#include "Creatures/Definition/CreatureDefinition.h"
#include "Creatures/Locomotion/CreatureLocomotionFacts.h"
#include <algorithm>
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>

namespace cutum
{

struct CreaturePartPose
{
  glm::vec3 offsetDelta{0.0f};
  glm::vec3 eulerDeg{0.0f};
};

struct CreaturePoseParams
{
  std::unordered_map<std::string, CreaturePartPose> Parts;
  float crouchUpperDrop{0.0f};

  void SetPart(const std::string &Id, CreaturePartPose pose)
  {
    Parts[Id] = pose;
  }
};

/// Gait swing amplitude from actual travel speed (shared by the rigid pose
/// presenters and the bone-skeleton pose engine; was four diverging copies).
/// `minScale` differs per rig family: bipeds/aerials hold 0.5, long torsos
/// (quadrupeds, bone rigs) 0.35.
inline float WalkSwingScale(const CreatureLocomotionFacts &facts,
                            const CreatureDefinition &def,
                            float minScale = 0.35f)
{
  const float walkSpeed = std::max(def.locomotion.walkSpeed, 0.01f);
  if (facts.horizontalSpeed < 0.05f)
  {
    return 0.0f;
  }
  float swingScale =
      std::clamp(facts.horizontalSpeed / walkSpeed, minScale, 1.5f);
  if (facts.state == LocomotionState::Run &&
      facts.horizontalSpeed > walkSpeed * 1.2f)
  {
    swingScale *= def.visual.Animation.runSpeedMultiplier;
  }
  return swingScale;
}

} // namespace cutum

#endif
