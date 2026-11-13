#pragma once
#include "Creatures/Locomotion/CreatureLocomotionFacts.h"
#include "Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonTypes.h"
#include <array>
namespace cutum {
enum class HumanMotionMode { Ground, Flight, Surface, Submerged, Jump, Fall };
struct HumanMotionInput {
  CreatureLocomotionFacts facts;
  glm::vec3 feet{0.f};
  float yawDeg{0.f};
  float scale{.9f};
  bool rightHandBusy{false}, leftHandBusy{false};
};
struct HumanMotionState {
  bool initialized{false}, submerged{false}, wasGrounded{true};
  HumanMotionMode mode{HumanMotionMode::Ground};
  float distancePhase{0.f}, idlePhase{0.f}, landing{0.f}, previousVerticalSpeed{0.f};
  unsigned landingEvents{0};
  float audioPhase{0.f};
  float actionRemaining{0.f};
  BoneSkeletonPose blended;
  std::array<glm::vec3,2> contacts{};
  std::array<bool,2> planted{{false,false}};
  std::array<bool,2> stanceLast{{false,false}}, unreachable{{false,false}};
  std::array<float,2> ikWeights{{1.f,1.f}};
  glm::vec3 previousFeet{0.f};
};
}
