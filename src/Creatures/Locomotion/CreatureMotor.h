#ifndef CREATUREMOTOR_H
#define CREATUREMOTOR_H

#include "Creatures/Locomotion/CreatureLocomotionController.h"
#include <cstdint>
#include <glm/glm.hpp>

namespace cutum
{

class UWorld;
using CreatureId = uint64_t;

struct CreatureMotorHorizontalResult
{
  glm::vec3 eyePos{0.0f};
  bool moved{false};
  bool wantsStepUpAnim{false};
  glm::vec3 stepUpStart{0.0f};
  glm::vec3 stepUpTarget{0.0f};
};

/// Shared horizontal resolve + optional step-up (player Camera and NPC).
/// `wishDirWorld` should be unit length or zero; Y is used when flying / swim.
/// When `instantStepUp` is true, landing is applied immediately (NPC).
/// When false, a valid step-up sets `wantsStepUpAnim` for Camera animation.
CreatureMotorHorizontalResult ApplyCreatureMotorHorizontal(
    const UWorld &world, const glm::vec3 &eyePos,
    UCreatureLocomotionController &locomotion, const glm::vec3 &wishDirWorld,
    float speed, float dt, CreatureId skipCreatureId, bool stepUpEnabled,
    bool jumpHeld, bool instantStepUp);

/// Horizontal motor + vertical `UpdateLocomotion` (NPC per-frame path).
void ApplyCreatureMotorStep(const UWorld &world, glm::vec3 &eyePos,
                            UCreatureLocomotionController &locomotion,
                            const CreatureInput &verticalInput,
                            const glm::vec3 &wishDirWorld, float speed, float dt,
                            CreatureId skipCreatureId, bool stepUpEnabled);

} // namespace cutum

#endif
