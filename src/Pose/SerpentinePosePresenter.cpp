#include "Pose/SerpentinePosePresenter.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include <cmath>

namespace cutum
{

LocomotionArchetype USerpentinePosePresenter::GetArchetype() const
{
  return LocomotionArchetype::Serpentine;
}

CreaturePoseParams USerpentinePosePresenter::Compute(
    const CreatureLocomotionFacts &facts, const CreatureDefinition &def,
    float /*dt*/)
{
  CreaturePoseParams pose;
  const float sinP = std::sin(facts.animPhase);
  const float sway = def.visual.Animation.tailSwingDeg;

  switch (facts.state)
  {
  case LocomotionState::Slither:
  {
    CreaturePartPose torso;
    torso.eulerDeg = glm::vec3(0.0f, sinP * sway, 0.0f);
    pose.SetPart("torso", torso);
    pose.SetPart("body", torso);
    CreaturePartPose tail;
    tail.eulerDeg = glm::vec3(0.0f, -sinP * sway * 1.2f, 0.0f);
    pose.SetPart("tail", tail);
    break;
  }
  case LocomotionState::Coil:
  {
    CreaturePartPose torso;
    torso.eulerDeg = glm::vec3(0.0f, sinP * sway * 0.5f, 0.0f);
    pose.SetPart("torso", torso);
    pose.SetPart("body", torso);
    break;
  }
  default:
    break;
  }
  return pose;
}

} // namespace cutum
