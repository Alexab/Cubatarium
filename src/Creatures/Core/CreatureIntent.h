#ifndef CREATUREINTENT_H
#define CREATUREINTENT_H

#include "Creatures/Influence/InfluenceTypes.h"
#include "Creatures/Locomotion/LocomotionTypes.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <string>

namespace cutum
{

/// Desired influence for this tick (melee/dig/use/aura). Sole attack/dig SoT.
struct InfluenceIntent
{
  InfluenceChannel Channel{InfluenceChannel::None};
  uint64_t TargetId{0};
  glm::vec3 TargetPoint{0.0f};
  bool HasTargetPoint{false};
  glm::ivec3 TargetBlockPos{0};
  bool HasTargetBlock{false};
  std::string ActionId;
};

struct CreatureIntent
{
  glm::vec3 moveDirWorld{0.0f};
  float moveSpeed{0.0f};
  bool wantJump{false};
  bool clearOnApply{true};
  LocomotionState suggestedAnim{LocomotionState::Idle};
  glm::vec3 lookAtWorld{0.0f};
  float lookAtWeight{0.0f};
  /// Deprecated: unused; melee targets live in Influence.TargetId only.
  uint64_t attackTargetId{0};
  InfluenceIntent Influence{};
};

} // namespace cutum

#endif
