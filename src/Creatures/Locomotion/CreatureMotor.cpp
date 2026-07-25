#include "Creatures/Locomotion/CreatureMotor.h"

#include "Creatures/Player/PlayerCapsule.h"
#include "World/Core/World.h"
#include "World/Math/FluidCellState.h"
#include <algorithm>
#include <cmath>

namespace cutum
{

namespace
{
constexpr float kStepUpTriggerDistance = 0.36f;
} // namespace

CreatureMotorHorizontalResult ApplyCreatureMotorHorizontal(
    const UWorld &world, const glm::vec3 &eyePos,
    UCreatureLocomotionController &locomotion, const glm::vec3 &wishDirWorld,
    float speed, float dt, CreatureId skipCreatureId, bool stepUpEnabled,
    bool jumpHeld, bool instantStepUp)
{
  CreatureMotorHorizontalResult result;
  result.eyePos = eyePos;

  glm::vec3 wish = wishDirWorld;
  const float wishLenSq = glm::dot(wish, wish);
  if (wishLenSq > 1e-10f)
  {
    wish /= std::sqrt(wishLenSq);
  }
  else
  {
    wish = glm::vec3(0.0f);
  }

  glm::vec3 shift = wish * (speed * dt);
  const bool hasShift = glm::dot(shift, shift) > 1e-10f;
  const PlayerCapsule cap = locomotion.GetStanceBlend() > 0.05f
                                ? locomotion.GetCollisionCapsule()
                                : locomotion.GetCapsule();
  // Unstick overlapping mobs even when wish is zero this frame.
  {
    glm::vec3 body(eyePos.x, cap.feetY(eyePos), eyePos.z);
    const glm::vec3 size = SizeBlocksFromCapsule(cap);
    world.DepenetrateCreatureBodyXZ(body, size, skipCreatureId);
    result.eyePos = glm::vec3(body.x, body.y + cap.eyeHeight, body.z);
  }
  glm::vec3 startEye = result.eyePos;
  const SampledFluidState fluid = world.SampleFluidPhysics(startEye, cap);
  if (hasShift && fluid.inFluid)
  {
    const float drag =
        1.0f - std::min(0.95f, fluid.DragHorizontal * dt * 8.0f);
    shift.x *= drag;
    shift.z *= drag;
  }

  glm::vec3 newPos = startEye;
  if (hasShift)
  {
    newPos = world.ResolveMovement(startEye, shift, cap, skipCreatureId);
  }

  const bool grounded =
      world.HasGroundSupport(startEye, cap) || locomotion.IsOnGround();
  bool stepped = false;
  if (stepUpEnabled && !fluid.inFluid && grounded && locomotion.IsOnGround() &&
      locomotion.IsFeetAnchored() && !jumpHeld &&
      locomotion.GetVerticalVelocity() <= 0.05f &&
      glm::dot(wish, wish) > 1e-10f)
  {
    const glm::vec3 intentXZ(wish.x, 0.0f, wish.z);
    const float intentLenSq = glm::dot(intentXZ, intentXZ);
    if (intentLenSq > 1e-10f)
    {
      const glm::vec3 intent = intentXZ / std::sqrt(intentLenSq);
      const UWorld::StepUpProbe probe =
          world.ProbeStepUp(newPos, intent, cap, kStepUpTriggerDistance);
      if (probe.Valid)
      {
        glm::vec3 landing = newPos;
        if (world.GetStepUpLanding(newPos, intent, cap, kStepUpTriggerDistance,
                                   landing))
        {
          if (instantStepUp)
          {
            newPos = landing;
            locomotion.SyncAfterStepLanding(newPos, &world);
            stepped = true;
          }
          else
          {
            result.wantsStepUpAnim = true;
            result.stepUpStart = newPos;
            result.stepUpTarget = landing;
            stepped = true;
          }
        }
      }
    }
  }

  if (!result.wantsStepUpAnim)
  {
    result.eyePos = newPos;
  }
  result.moved = hasShift || stepped ||
                 glm::length(glm::vec2(result.eyePos.x - eyePos.x,
                                       result.eyePos.z - eyePos.z)) > 1e-4f;
  return result;
}

void ApplyCreatureMotorStep(const UWorld &world, glm::vec3 &eyePos,
                            UCreatureLocomotionController &locomotion,
                            const CreatureInput &verticalInput,
                            const glm::vec3 &wishDirWorld, float speed, float dt,
                            CreatureId skipCreatureId, bool stepUpEnabled)
{
  const CreatureMotorHorizontalResult horiz = ApplyCreatureMotorHorizontal(
      world, eyePos, locomotion, wishDirWorld, speed, dt, skipCreatureId,
      stepUpEnabled, verticalInput.jumpHeld, true);
  eyePos = horiz.eyePos;
  locomotion.UpdateLocomotion(&world, eyePos, verticalInput, dt,
                              skipCreatureId);
}

} // namespace cutum
