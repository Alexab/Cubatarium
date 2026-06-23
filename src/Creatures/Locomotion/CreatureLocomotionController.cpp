#include "Creatures/Locomotion/CreatureLocomotionController.h"
#include "Creatures/Core/CreatureBounds.h"
#include "World/Core/World.h"
#include "World/Math/GridMath.h"
#include <algorithm>
#include <cmath>

namespace cutum
{

namespace
{

CreatureLocomotionCapabilities
ClampCapabilities(CreatureLocomotionCapabilities caps)
{
  caps.jumpHeightBlocks = std::clamp(caps.jumpHeightBlocks, 0.25f, 3.0f);
  caps.walkSpeed = std::clamp(caps.walkSpeed, 0.5f, 20.0f);
  caps.sprintSpeedMultiplier =
      std::clamp(caps.sprintSpeedMultiplier, 0.1f, 5.0f);
  caps.crouchSpeedMultiplier =
      std::clamp(caps.crouchSpeedMultiplier, 0.1f, 5.0f);
  caps.flySpeedMultiplier = std::clamp(caps.flySpeedMultiplier, 0.1f, 5.0f);
  caps.flySpeed = caps.walkSpeed * caps.flySpeedMultiplier;
  return caps;
}

bool IsMovingInput(const CreatureInput &input)
{
  return input.MoveForward || input.MoveBack || input.MoveLeft ||
         input.MoveRight;
}

} // namespace

bool UCreatureLocomotionController::IsSprinting(
    const CreatureInput &input) const
{
  if (Mode == CreatureMovementMode::Flying || !Caps.canSprint ||
      !input.sprintHeld || !IsMovingInput(input))
  {
    return false;
  }
  if (Caps.canCrouch && input.crouchHeld)
  {
    return false;
  }
  return true;
}

float UCreatureLocomotionController::ResolveHorizontalSpeed(
    const CreatureInput &input) const
{
  if (Mode == CreatureMovementMode::Flying)
  {
    return Caps.walkSpeed * Caps.flySpeedMultiplier;
  }
  if (Caps.canCrouch && input.crouchHeld)
  {
    return Caps.walkSpeed * Caps.crouchSpeedMultiplier;
  }
  if (IsSprinting(input))
  {
    return Caps.walkSpeed * Caps.sprintSpeedMultiplier;
  }
  return Caps.walkSpeed;
}

void UCreatureLocomotionController::SetCapabilities(
    const CreatureLocomotionCapabilities &caps)
{
  Caps = ClampCapabilities(caps);
  JumpSpeed = Caps.canJump ? JumpSpeedFromHeight(Caps.jumpHeightBlocks,
                                                 kGravityMagnitude)
                           : 0.0f;
}

void UCreatureLocomotionController::SetCollisionProfile(
    const glm::vec3 &sizeBlocks, float eyeHeight)
{
  CollisionSizeBlocks = sizeBlocks;
  EyeHeight = eyeHeight;
}

void UCreatureLocomotionController::Reset()
{
  Mode = CreatureMovementMode::Walking;
  LocomotionState = LocomotionState::Idle;
  StanceBlend = 0.0f;
  UnsupportedFluidStreak = 0;
  FeetY = 0.0f;
  FeetAnchored = false;
  OnGround = true;
  VerticalVelocity = 0.0f;
  SpaceWasPressed = false;
  SuppressNextJump = false;
  ClearShiftRequest = false;
  LastSpacePressTime = {};
}

void UCreatureLocomotionController::SetMode(CreatureMovementMode mode)
{
  Mode = mode;
  if (mode == CreatureMovementMode::Flying)
  {
    LocomotionState = LocomotionState::Fly;
    StanceBlend = 0.0f;
    FeetAnchored = false;
    OnGround = false;
  }
  else
  {
    VerticalVelocity = 0.0f;
    SuppressNextJump = false;
  }
}

void UCreatureLocomotionController::updateLocomotionState(
    const CreatureInput &input)
{
  if (Mode == CreatureMovementMode::Flying)
  {
    LocomotionState = LocomotionState::Fly;
    return;
  }
  if (StanceBlend > kJumpStanceMax)
  {
    LocomotionState = LocomotionState::Crouch;
    return;
  }
  if (!OnGround && VerticalVelocity < -0.5f)
  {
    LocomotionState = LocomotionState::Fall;
    return;
  }
  if (!OnGround && VerticalVelocity > 0.5f)
  {
    LocomotionState = LocomotionState::Jump;
    return;
  }
  const bool moving = IsMovingInput(input);
  if (OnGround && moving)
  {
    LocomotionState =
        IsSprinting(input) ? LocomotionState::Run : LocomotionState::Walk;
    return;
  }
  LocomotionState = LocomotionState::Idle;
}

PlayerCapsule UCreatureLocomotionController::GetCapsule() const
{
  return PlayerCapsule::FromCreatureBlocks(CollisionSizeBlocks, EyeHeight);
}

PlayerCapsule UCreatureLocomotionController::GetCollisionCapsule() const
{
  const PlayerCapsule stand = GetCapsule();
  const float t = std::clamp(StanceBlend, 0.0f, 1.0f);
  if (t <= 0.0f)
  {
    return stand;
  }
  const PlayerCapsule refStand = PlayerCapsule::Standing();
  const PlayerCapsule refCrouch = PlayerCapsule::Crouching();
  const float crouchHeight =
      stand.height * (refCrouch.height / refStand.height);
  const float crouchEye =
      stand.eyeHeight * (refCrouch.eyeHeight / refStand.eyeHeight);
  return {stand.height + (crouchHeight - stand.height) * t,
          stand.eyeHeight + (crouchEye - stand.eyeHeight) * t,
          stand.halfWidth};
}

bool UCreatureLocomotionController::OnSpacePressed()
{
  if (!Caps.canFly)
  {
    return false;
  }
  const auto now = std::chrono::steady_clock::now();
  if (LastSpacePressTime.time_since_epoch().count() != 0)
  {
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - LastSpacePressTime)
                        .count();
    if (ms >= 0 && ms < kDoubleSpaceTapMs)
    {
      const bool enabling = Mode == CreatureMovementMode::Walking;
      SetMode(enabling ? CreatureMovementMode::Flying
                       : CreatureMovementMode::Walking);
      SuppressNextJump = enabling;
      LastSpacePressTime = {};
      SpaceWasPressed = true;
      return true;
    }
  }
  LastSpacePressTime = now;
  return false;
}

bool UCreatureLocomotionController::ConsumeClearShiftRequest()
{
  const bool request = ClearShiftRequest;
  ClearShiftRequest = false;
  return request;
}

void UCreatureLocomotionController::OnLandedFromFlight(const UWorld *world,
                                                       glm::vec3 &eyePos,
                                                       bool /*clearShiftKeys*/)
{
  SetMode(CreatureMovementMode::Walking);
  landStanding(world, eyePos, world ? world->GetMovementCollisionSkipId() : 0);
  SpaceWasPressed = true;
  SuppressNextJump = false;
}

void UCreatureLocomotionController::SyncAfterStepLanding(glm::vec3 &eyePos,
                                                         const UWorld *world)
{
  OnGround = true;
  VerticalVelocity = 0.0f;
  if (world)
  {
    anchorFeetFromStandingEye(world, eyePos);
    syncEyeFromFeet(eyePos);
  }
  updateLocomotionState({});
}

bool UCreatureLocomotionController::anchorFeetFromStandingEye(
    const UWorld *world, const glm::vec3 &eyePos)
{
  if (!world)
  {
    return false;
  }
  const PlayerCapsule cap = GetCapsule();
  if (!world->HasGroundSupport(eyePos, cap))
  {
    FeetAnchored = false;
    return false;
  }
  FeetY = cap.feetY(eyePos);
  FeetAnchored = true;
  return true;
}

float UCreatureLocomotionController::GetViewEyeHeight() const
{
  const PlayerCapsule stand = GetCapsule();
  const float crouchEye = EyeHeight * 0.85f;
  const float t = std::clamp(StanceBlend, 0.0f, 1.0f);
  return stand.eyeHeight + (crouchEye - stand.eyeHeight) * t;
}

void UCreatureLocomotionController::SetStanceBlendForView(float blend01)
{
  StanceBlend = std::clamp(blend01, 0.0f, 1.0f);
}

void UCreatureLocomotionController::SyncFeetAnchorFromView(float feetY,
                                                           bool anchored)
{
  FeetY = feetY;
  FeetAnchored = anchored;
}

void UCreatureLocomotionController::syncEyeFromFeet(glm::vec3 &eyePos) const
{
  if (!FeetAnchored)
  {
    return;
  }
  eyePos.y = FeetY + GetViewEyeHeight();
}

void UCreatureLocomotionController::landStanding(const UWorld *world,
                                                 glm::vec3 &eyePos,
                                                 CreatureId skipCreatureId)
{
  StanceBlend = 0.0f;
  if (!world)
  {
    OnGround = false;
    FeetAnchored = false;
    return;
  }
  if (!anchorFeetFromStandingEye(world, eyePos))
  {
    OnGround = false;
    return;
  }
  const PlayerCapsule cap = GetCapsule();
  if (!world->IsValidStandFootprint(eyePos, cap, FeetY))
  {
    OnGround = false;
    FeetAnchored = false;
    return;
  }
  eyePos.y = FeetY + cap.eyeHeight;
  if (world->CheckCollision(eyePos, cap, skipCreatureId))
  {
    OnGround = false;
    FeetAnchored = false;
    return;
  }

  OnGround = true;
  VerticalVelocity = 0.0f;
  syncEyeFromFeet(eyePos);
}

bool UCreatureLocomotionController::canStandUpAt(
    const UWorld *world, const glm::vec3 &eyePos,
    CreatureId skipCreatureId) const
{
  if (!world || !FeetAnchored)
  {
    return true;
  }
  const PlayerCapsule stand = GetCapsule();
  const glm::vec3 trialEye(eyePos.x, FeetY + stand.eyeHeight, eyePos.z);
  return !world->CheckCollision(trialEye, stand, skipCreatureId);
}

void UCreatureLocomotionController::updateStanceBlend(
    const UWorld *world, const glm::vec3 &eyePos, const CreatureInput &input,
    float dt, CreatureId skipCreatureId)
{
  if (Mode == CreatureMovementMode::Flying)
  {
    StanceBlend = 0.0f;
    return;
  }

  float target = 0.0f;
  if (Caps.canCrouch && world && FeetAnchored && OnGround &&
      VerticalVelocity <= 0.05f && input.crouchHeld && !ClearShiftRequest)
  {
    target = 1.0f;
  }
  if (target < 1.0f && StanceBlend > kJumpStanceMax &&
      !canStandUpAt(world, eyePos, skipCreatureId) &&
      (input.crouchHeld || StanceBlend > 0.5f))
  {
    target = 1.0f;
  }

  const float step = dt / kStanceTransitionDuration;
  if (StanceBlend < target)
  {
    StanceBlend = std::min(target, StanceBlend + step);
  }
  else if (StanceBlend > target)
  {
    StanceBlend = std::max(target, StanceBlend - step);
  }
}

bool UCreatureLocomotionController::tryStandFromCrouch(
    const UWorld *world, glm::vec3 &eyePos, const CreatureInput &input,
    CreatureId skipCreatureId)
{
  if (StanceBlend <= kJumpStanceMax || !OnGround || !FeetAnchored)
  {
    return false;
  }
  if (!canStandUpAt(world, eyePos, skipCreatureId))
  {
    return false;
  }
  StanceBlend = 0.0f;
  syncEyeFromFeet(eyePos);
  if (input.crouchHeld)
  {
    ClearShiftRequest = true;
  }
  return true;
}

bool UCreatureLocomotionController::tryJump(const UWorld *world,
                                            glm::vec3 &eyePos,
                                            const CreatureInput & /*input*/,
                                            CreatureId skipCreatureId)
{
  if (!Caps.canJump || SuppressNextJump || StanceBlend > kJumpStanceMax)
  {
    return false;
  }
  if (!OnGround || !FeetAnchored)
  {
    return false;
  }
  const PlayerCapsule cap = GetCollisionCapsule();
  if (world && world->CheckCollision(eyePos, cap, skipCreatureId))
  {
    return false;
  }
  FeetAnchored = false;
  OnGround = false;
  StanceBlend = 0.0f;
  VerticalVelocity = JumpSpeed;
  return true;
}

void UCreatureLocomotionController::syncGroundedPose(const UWorld *world,
                                                     glm::vec3 &eyePos,
                                                     const CreatureInput &input,
                                                     float dt,
                                                     CreatureId skipCreatureId)
{
  updateStanceBlend(world, eyePos, input, dt, skipCreatureId);

  const PlayerCapsule cap = GetCollisionCapsule();
  if (world && world->CheckCollision(eyePos, cap, skipCreatureId))
  {
    if (!world->DepenetrateEye(eyePos, cap, skipCreatureId))
    {
      OnGround = false;
      FeetAnchored = false;
      VerticalVelocity = 0.0f;
      return;
    }
  }

  OnGround = true;
  VerticalVelocity = 0.0f;

  if (StanceBlend > kJumpStanceMax)
  {
    if (!FeetAnchored)
    {
      anchorFeetFromStandingEye(world, eyePos);
    }
  }
  else
  {
    anchorFeetFromStandingEye(world, eyePos);
  }
  syncEyeFromFeet(eyePos);
}

void UCreatureLocomotionController::UpdateLocomotion(const UWorld *world,
                                                     glm::vec3 &eyePos,
                                                     const CreatureInput &input,
                                                     float dt,
                                                     CreatureId skipCreatureId)
{
  if (!world)
  {
    return;
  }

  if (Mode == CreatureMovementMode::Flying)
  {
    StanceBlend = 0.0f;
    const PlayerCapsule cap = GetCapsule();
    if (world->HasGroundSupport(eyePos, cap))
    {
      OnLandedFromFlight(world, eyePos, false);
    }
    if (!input.jumpHeld)
    {
      SuppressNextJump = false;
    }
    SpaceWasPressed = input.jumpHeld;
    updateLocomotionState(input);
    return;
  }

  const PlayerCapsule cap = GetCollisionCapsule();
  const UWorld::SampledFluidState fluid =
      world->SampleFluidPhysics(eyePos, cap);

  const CollisionVolume supportVol = CollisionVolumeFromEye(eyePos, cap);
  const float feetY = supportVol.center.y - supportVol.halfExtents.y;
  const bool supported = world->HasGroundSupportVolume(supportVol, feetY);

  if (fluid.inFluid && !supported)
  {
    UnsupportedFluidStreak = std::min(UnsupportedFluidStreak + 1, 8);
  }
  else
  {
    UnsupportedFluidStreak = 0;
  }
  const bool swimmingFluid = fluid.inFluid && UnsupportedFluidStreak >= 2;

  if (!input.jumpHeld)
  {
    SuppressNextJump = false;
  }

  const bool jumpEdge = input.jumpHeld && !SpaceWasPressed;
  const bool jumpFromGround =
      FeetAnchored && (OnGround || (supported && !swimmingFluid));
  if (jumpEdge && !SuppressNextJump && jumpFromGround)
  {
    if (StanceBlend > kJumpStanceMax &&
        canStandUpAt(world, eyePos, skipCreatureId))
    {
      if (tryStandFromCrouch(world, eyePos, input, skipCreatureId))
      {
        SpaceWasPressed = true;
        updateLocomotionState(input);
        return;
      }
    }
    else if (StanceBlend <= kJumpStanceMax &&
             tryJump(world, eyePos, input, skipCreatureId))
    {
      SpaceWasPressed = true;
      updateLocomotionState(input);
      return;
    }
  }

  if (swimmingFluid)
  {
    OnGround = false;
    FeetAnchored = false;
    if (jumpEdge)
    {
      VerticalVelocity = std::max(VerticalVelocity, 6.5f);
    }
    if (input.jumpHeld)
    {
      VerticalVelocity += fluid.RiseSpeed * 5.0f * dt;
    }
    VerticalVelocity *= 1.0f - std::min(0.9f, fluid.DragHorizontal * dt * 6.0f);
    if (!input.jumpHeld)
    {
      VerticalVelocity -= fluid.SinkSpeed * dt;
    }
    VerticalVelocity += kGravity * 0.15f * dt;
    const glm::vec3 verticalDelta(0.0f, VerticalVelocity * dt, 0.0f);
    eyePos = world->ResolveMovement(eyePos, verticalDelta, cap, skipCreatureId);
    updateStanceBlend(world, eyePos, input, dt, skipCreatureId);
    if (!input.crouchHeld && StanceBlend > 0.0f)
    {
      const float step = dt / kStanceTransitionDuration;
      StanceBlend = std::max(0.0f, StanceBlend - step * 2.5f);
    }
    SpaceWasPressed = input.jumpHeld;
    updateLocomotionState(input);
    return;
  }

  const bool groundedPose = VerticalVelocity <= 0.05f && supported;

  if (groundedPose)
  {
    syncGroundedPose(world, eyePos, input, dt, skipCreatureId);
  }
  else
  {
    OnGround = false;
    updateStanceBlend(world, eyePos, input, dt, skipCreatureId);

    if (world->CheckCollision(eyePos, cap, skipCreatureId))
    {
      world->DepenetrateEye(eyePos, cap, skipCreatureId);
    }

    VerticalVelocity += kGravity * dt;
    const glm::vec3 verticalDelta(0.0f, VerticalVelocity * dt, 0.0f);
    const glm::vec3 resolved =
        world->ResolveMovement(eyePos, verticalDelta, cap, skipCreatureId);
    const float movedY = resolved.y - eyePos.y;
    const float requestedY = verticalDelta.y;
    const bool hitFloor =
        std::abs(movedY - requestedY) > 1e-4f && requestedY < 0.0f;
    eyePos = resolved;
    if (hitFloor)
    {
      landStanding(world, eyePos, skipCreatureId);
    }
    else
    {
      FeetAnchored = false;
    }
  }

  SpaceWasPressed = input.jumpHeld;
  updateLocomotionState(input);
}

} // namespace cutum
