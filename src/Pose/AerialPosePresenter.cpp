#include "Pose/AerialPosePresenter.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include <cmath>
#include <glm/glm.hpp>

namespace cutum
{

LocomotionArchetype UAerialPosePresenter::GetArchetype() const
{
  return LocomotionArchetype::Aerial;
}

CreaturePoseParams
UAerialPosePresenter::Compute(const CreatureLocomotionFacts &facts,
                              const CreatureDefinition &def, float /*dt*/)
{
  CreaturePoseParams pose;
  const float flapDeg = 22.0f;
  const float phase = facts.animPhase;

  switch (facts.state)
  {
  case LocomotionState::Walk:
  case LocomotionState::Run:
  case LocomotionState::Fly:
  case LocomotionState::Hover:
  case LocomotionState::Glide:
  {
    const float sinP = std::sin(phase);
    CreaturePartPose wingL;
    wingL.eulerDeg = glm::vec3(sinP * flapDeg, 0.0f, 0.0f);
    CreaturePartPose wingR;
    wingR.eulerDeg = glm::vec3(-sinP * flapDeg, 0.0f, 0.0f);
    pose.SetPart("wing_l", wingL);
    pose.SetPart("wing_r", wingR);

    CreaturePartPose torso;
    torso.eulerDeg =
        glm::vec3(def.visual.Animation.flyBodyPitchDeg * 0.5f, 0.0f, 0.0f);
    pose.SetPart("torso", torso);
    break;
  }
  default:
  {
    const float idleFlap = std::sin(phase * 0.5f) * flapDeg * 0.35f;
    CreaturePartPose wingL;
    wingL.eulerDeg = glm::vec3(idleFlap, 0.0f, 0.0f);
    CreaturePartPose wingR;
    wingR.eulerDeg = glm::vec3(-idleFlap, 0.0f, 0.0f);
    pose.SetPart("wing_l", wingL);
    pose.SetPart("wing_r", wingR);
    break;
  }
  }

  if (facts.lookAtWeight > 0.0f || std::abs(facts.bodyPitch) > 0.01f)
  {
    CreaturePartPose head;
    head.eulerDeg = glm::vec3(facts.bodyPitch * 0.35f, 0.0f, 0.0f);
    pose.SetPart("head", head);
  }

  return pose;
}

} // namespace cutum
