#include "Creatures/Locomotion/LocomotionStateDerive.h"
#include <algorithm>
#include <cmath>

namespace cutum
{
namespace locomotion
{
constexpr float kMoveSpeedEpsilon = 0.05f;
constexpr float kRunSpeedFactor = 1.35f;
constexpr float kJumpVyThreshold = 0.5f;
constexpr float kFallVyThreshold = -0.5f;
constexpr float kCrouchStanceThreshold = 0.35f;
constexpr float kGlideVyThreshold = -0.3f;
} // namespace locomotion

bool IsAirborneLocomotionState(LocomotionState state)
{
  switch (state)
  {
  case LocomotionState::Jump:
  case LocomotionState::Fall:
  case LocomotionState::Fly:
  case LocomotionState::Glide:
  case LocomotionState::Hover:
    return true;
  default:
    return false;
  }
}

bool IsAquaticLocomotionState(LocomotionState state)
{
  switch (state)
  {
  case LocomotionState::Swim:
  case LocomotionState::Tread:
    return true;
  default:
    return false;
  }
}

namespace
{

LocomotionState DeriveTerrestrial(const CreatureLocomotionFacts &raw,
                                  const CreatureLocomotionCapabilities &caps,
                                  const CreatureLocomotionRawInput *hintInput)
{
  if (raw.mode == CreatureMovementMode::Flying)
  {
    return LocomotionState::Fly;
  }
  if (!raw.onGround && raw.verticalSpeed > locomotion::kJumpVyThreshold)
  {
    return LocomotionState::Jump;
  }
  if (!raw.onGround && raw.verticalSpeed < locomotion::kFallVyThreshold)
  {
    return LocomotionState::Fall;
  }
  if (raw.stanceBlend > locomotion::kCrouchStanceThreshold)
  {
    return LocomotionState::Crouch;
  }
  const float runThreshold =
      std::max(caps.walkSpeed * locomotion::kRunSpeedFactor,
               locomotion::kMoveSpeedEpsilon);
  if (raw.onGround && raw.horizontalSpeed > runThreshold)
  {
    return LocomotionState::Run;
  }
  if (raw.onGround && raw.horizontalSpeed > locomotion::kMoveSpeedEpsilon)
  {
    return LocomotionState::Walk;
  }
  if (hintInput && hintInput->hasSuggestedAnim)
  {
    switch (hintInput->suggestedAnim)
    {
    case LocomotionState::Walk:
    case LocomotionState::Run:
      if (raw.horizontalSpeed > locomotion::kMoveSpeedEpsilon)
      {
        return hintInput->suggestedAnim;
      }
      break;
    case LocomotionState::Crouch:
    case LocomotionState::Fly:
      return hintInput->suggestedAnim;
    default:
      break;
    }
  }
  return LocomotionState::Idle;
}

LocomotionState DeriveAerial(const CreatureLocomotionFacts &raw)
{
  if (raw.mode != CreatureMovementMode::Flying)
  {
    if (raw.onGround && raw.horizontalSpeed > locomotion::kMoveSpeedEpsilon)
    {
      return LocomotionState::Walk;
    }
    return LocomotionState::Idle;
  }
  if (raw.horizontalSpeed < locomotion::kMoveSpeedEpsilon)
  {
    return LocomotionState::Hover;
  }
  if (raw.verticalSpeed < locomotion::kGlideVyThreshold)
  {
    return LocomotionState::Glide;
  }
  return LocomotionState::Fly;
}

LocomotionState DeriveAquatic(const CreatureLocomotionFacts &raw)
{
  if (!raw.inFluid)
  {
    return LocomotionState::Idle;
  }
  if (raw.onFluidBottom && raw.horizontalSpeed > locomotion::kMoveSpeedEpsilon)
  {
    return LocomotionState::Tread;
  }
  if (raw.horizontalSpeed > locomotion::kMoveSpeedEpsilon)
  {
    return LocomotionState::Swim;
  }
  return LocomotionState::Idle;
}

LocomotionState DeriveSerpentine(const CreatureLocomotionFacts &raw)
{
  if (raw.onGround && raw.horizontalSpeed > locomotion::kMoveSpeedEpsilon)
  {
    return LocomotionState::Slither;
  }
  if (raw.stanceBlend > locomotion::kCrouchStanceThreshold &&
      raw.horizontalSpeed < locomotion::kMoveSpeedEpsilon)
  {
    return LocomotionState::Coil;
  }
  return LocomotionState::Idle;
}

} // namespace

LocomotionState
DeriveLocomotionState(LocomotionArchetype archetype,
                      const CreatureLocomotionFacts &raw,
                      const CreatureLocomotionCapabilities &caps,
                      const CreatureLocomotionRawInput *hintInput)
{
  switch (archetype)
  {
  case LocomotionArchetype::TerrestrialBiped:
  case LocomotionArchetype::TerrestrialQuadruped:
    return DeriveTerrestrial(raw, caps, hintInput);
  case LocomotionArchetype::Aerial:
    return DeriveAerial(raw);
  case LocomotionArchetype::Aquatic:
    return DeriveAquatic(raw);
  case LocomotionArchetype::Serpentine:
    return DeriveSerpentine(raw);
  }
  return LocomotionState::Idle;
}

} // namespace cutum
