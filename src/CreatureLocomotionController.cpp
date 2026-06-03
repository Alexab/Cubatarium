#include "CreatureLocomotionController.h"
#include "World.h"
#include <algorithm>
#include <cmath>

namespace cutum {

void CreatureLocomotionController::Reset()
{
 mode_ = CreatureMovementMode::Walking;
 locomotionState_ = LocomotionState::Idle;
 stanceBlend_ = 0.0f;
 feetY_ = 0.0f;
 feetAnchored_ = false;
 onGround_ = true;
 verticalVelocity_ = 0.0f;
 spaceWasPressed_ = false;
 suppressNextJump_ = false;
 clearShiftRequest_ = false;
 lastSpacePressTime_ = {};
}

void CreatureLocomotionController::SetMode(CreatureMovementMode mode)
{
 mode_ = mode;
 if (mode == CreatureMovementMode::Flying) {
  locomotionState_ = LocomotionState::Fly;
  stanceBlend_ = 0.0f;
  feetAnchored_ = false;
 } else {
  verticalVelocity_ = 0.0f;
  suppressNextJump_ = false;
 }
}

void CreatureLocomotionController::updateLocomotionState(const CreatureInput& input)
{
 if (mode_ == CreatureMovementMode::Flying) {
  locomotionState_ = LocomotionState::Fly;
  return;
 }
 if (stanceBlend_ > kJumpStanceMax) {
  locomotionState_ = LocomotionState::Crouch;
  return;
 }
 if (!onGround_ && verticalVelocity_ < -0.5f) {
  locomotionState_ = LocomotionState::Fall;
  return;
 }
 if (!onGround_ && verticalVelocity_ > 0.5f) {
  locomotionState_ = LocomotionState::Jump;
  return;
 }
 const bool moving = input.moveForward || input.moveBack || input.moveLeft || input.moveRight;
 if (onGround_ && moving) {
  locomotionState_ = LocomotionState::Walk;
  return;
 }
 locomotionState_ = LocomotionState::Idle;
}

PlayerCapsule CreatureLocomotionController::GetCapsule() const
{
 return PlayerCapsule::Lerp(stanceBlend_);
}

bool CreatureLocomotionController::OnSpacePressed()
{
 if (!caps_.canFly) {
  return false;
 }
 const auto now = std::chrono::steady_clock::now();
 if (lastSpacePressTime_.time_since_epoch().count() != 0) {
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      now - lastSpacePressTime_).count();
  if (ms >= 0 && ms < kDoubleSpaceTapMs) {
   const bool enabling = mode_ == CreatureMovementMode::Walking;
   SetMode(enabling ? CreatureMovementMode::Flying : CreatureMovementMode::Walking);
   suppressNextJump_ = enabling;
   lastSpacePressTime_ = {};
   spaceWasPressed_ = true;
   return true;
  }
 }
 lastSpacePressTime_ = now;
 return false;
}

bool CreatureLocomotionController::ConsumeClearShiftRequest()
{
 const bool request = clearShiftRequest_;
 clearShiftRequest_ = false;
 return request;
}

void CreatureLocomotionController::OnLandedFromFlight(const World* world, glm::vec3& eyePos,
                                                      bool /*clearShiftKeys*/)
{
 SetMode(CreatureMovementMode::Walking);
 landStanding(world, eyePos);
 spaceWasPressed_ = true;
 suppressNextJump_ = false;
}

void CreatureLocomotionController::SyncAfterStepLanding(glm::vec3& eyePos, const World* world)
{
 onGround_ = true;
 verticalVelocity_ = 0.0f;
 if (world) {
  anchorFeetFromStandingEye(world, eyePos);
  if (stanceBlend_ > kJumpStanceMax) {
   applyCrouchEyeFromFeet(eyePos);
  }
 }
 updateLocomotionState({});
}

bool CreatureLocomotionController::anchorFeetFromStandingEye(const World* world,
                                                             const glm::vec3& eyePos)
{
 if (!world) {
  return false;
 }
 const PlayerCapsule stand = PlayerCapsule::Standing();
 if (!world->HasGroundSupport(eyePos, stand)) {
  feetAnchored_ = false;
  return false;
 }
 const float probeFeetY = stand.feetY(eyePos);
 const int supportY = static_cast<int>(std::floor(probeFeetY - 0.04f));
 feetY_ = static_cast<float>(supportY) + 1.0f;
 feetAnchored_ = true;
 return true;
}

void CreatureLocomotionController::applyCrouchEyeFromFeet(glm::vec3& eyePos) const
{
 if (!feetAnchored_) {
  return;
 }
 const PlayerCapsule cap = PlayerCapsule::Lerp(stanceBlend_);
 eyePos.y = feetY_ + cap.eyeHeight;
}

void CreatureLocomotionController::landStanding(const World* world, glm::vec3& eyePos)
{
 stanceBlend_ = 0.0f;
 onGround_ = true;
 verticalVelocity_ = 0.0f;
 if (!world) {
  feetAnchored_ = false;
  return;
 }
 if (!anchorFeetFromStandingEye(world, eyePos)) {
  return;
 }
 const PlayerCapsule stand = PlayerCapsule::Standing();
 eyePos.y = feetY_ + stand.eyeHeight;
 for (int i = 0; i < 32 && world->CheckCollision(eyePos, stand); ++i) {
  eyePos.y += 0.1f;
 }
 anchorFeetFromStandingEye(world, eyePos);
}

bool CreatureLocomotionController::canStandUpAt(const World* world,
                                                 const glm::vec3& eyePos) const
{
 if (!world || !feetAnchored_) {
  return true;
 }
 const glm::vec3 trialEye(eyePos.x, feetY_ + PlayerCapsule::Standing().eyeHeight, eyePos.z);
 return !world->CheckCollision(trialEye, PlayerCapsule::Standing());
}

void CreatureLocomotionController::updateStanceBlend(const World* world, const glm::vec3& eyePos,
                                                     const CreatureInput& input, float dt)
{
 if (mode_ == CreatureMovementMode::Flying) {
  stanceBlend_ = 0.0f;
  return;
 }

 float target = 0.0f;
 if (caps_.canCrouch && world && feetAnchored_ && onGround_ && verticalVelocity_ <= 0.05f &&
     input.crouchHeld) {
  target = 1.0f;
 }
 if (target < 1.0f && stanceBlend_ > 0.0f && !canStandUpAt(world, eyePos)) {
  target = 1.0f;
 }

 const float step = dt / kStanceTransitionDuration;
 if (stanceBlend_ < target) {
  stanceBlend_ = std::min(target, stanceBlend_ + step);
 } else if (stanceBlend_ > target) {
  stanceBlend_ = std::max(target, stanceBlend_ - step);
 }
}

bool CreatureLocomotionController::tryStandFromCrouch(const World* world, glm::vec3& eyePos,
                                                      const CreatureInput& input)
{
 if (stanceBlend_ <= kJumpStanceMax || !onGround_ || !feetAnchored_) {
  return false;
 }
 if (!canStandUpAt(world, eyePos)) {
  return false;
 }
 stanceBlend_ = 0.0f;
 applyCrouchEyeFromFeet(eyePos);
 if (input.crouchHeld) {
  clearShiftRequest_ = true;
 }
 return true;
}

bool CreatureLocomotionController::tryJump(const CreatureInput& /*input*/)
{
 if (!caps_.canJump || suppressNextJump_ || stanceBlend_ > kJumpStanceMax) {
  return false;
 }
 if (!onGround_ || !feetAnchored_) {
  return false;
 }
 feetAnchored_ = false;
 onGround_ = false;
 stanceBlend_ = 0.0f;
 verticalVelocity_ = kJumpSpeed;
 return true;
}

void CreatureLocomotionController::syncGroundedPose(const World* world, glm::vec3& eyePos,
                                                    const CreatureInput& input, float dt)
{
 onGround_ = true;
 verticalVelocity_ = 0.0f;
 updateStanceBlend(world, eyePos, input, dt);

 if (stanceBlend_ > kJumpStanceMax) {
  if (!feetAnchored_) {
   anchorFeetFromStandingEye(world, eyePos);
  }
  applyCrouchEyeFromFeet(eyePos);
 } else {
  anchorFeetFromStandingEye(world, eyePos);
 }
}

void CreatureLocomotionController::UpdateLocomotion(const World* world, glm::vec3& eyePos,
                                                   const CreatureInput& input, float dt)
{
 if (!world) {
  return;
 }

 if (mode_ == CreatureMovementMode::Flying) {
  stanceBlend_ = 0.0f;
  const PlayerCapsule stand = PlayerCapsule::Standing();
  if (world->HasGroundSupport(eyePos, stand)) {
   OnLandedFromFlight(world, eyePos, false);
  }
  if (!input.jumpHeld) {
   suppressNextJump_ = false;
  }
  spaceWasPressed_ = input.jumpHeld;
  updateLocomotionState(input);
  return;
 }

 const PlayerCapsule cap = GetCapsule();
 const World::SampledFluidState fluid = world->SampleFluidPhysics(eyePos, cap);

 if (!input.jumpHeld) {
  suppressNextJump_ = false;
 }

 const bool jumpEdge = input.jumpHeld && !spaceWasPressed_;
 if (jumpEdge && !suppressNextJump_ && onGround_ && feetAnchored_) {
  if (stanceBlend_ > kJumpStanceMax && canStandUpAt(world, eyePos)) {
   if (tryStandFromCrouch(world, eyePos, input)) {
    spaceWasPressed_ = true;
   }
  } else if (stanceBlend_ <= kJumpStanceMax && tryJump(input)) {
   spaceWasPressed_ = true;
   updateLocomotionState(input);
   return;
  }
 }

 if (fluid.inFluid) {
  onGround_ = false;
  feetAnchored_ = false;
  if (jumpEdge) {
   verticalVelocity_ = std::max(verticalVelocity_, 6.5f);
  }
  if (input.jumpHeld) {
   verticalVelocity_ += fluid.riseSpeed * 5.0f * dt;
  }
  verticalVelocity_ *= 1.0f - std::min(0.9f, fluid.dragHorizontal * dt * 6.0f);
  if (!input.jumpHeld) {
   verticalVelocity_ -= fluid.sinkSpeed * dt;
  }
  verticalVelocity_ += kGravity * 0.15f * dt;
  const glm::vec3 verticalDelta(0.0f, verticalVelocity_ * dt, 0.0f);
  eyePos = world->ResolveMovement(eyePos, verticalDelta, cap);
  updateStanceBlend(world, eyePos, input, dt);
  if (stanceBlend_ > kJumpStanceMax && feetAnchored_) {
   applyCrouchEyeFromFeet(eyePos);
  }
  spaceWasPressed_ = input.jumpHeld;
  updateLocomotionState(input);
  return;
 }

 const bool supported = world->HasGroundSupport(eyePos, cap);
 const bool groundedPose = verticalVelocity_ <= 0.05f && supported;

 if (groundedPose) {
  syncGroundedPose(world, eyePos, input, dt);
 } else {
  onGround_ = false;
  updateStanceBlend(world, eyePos, input, dt);

  verticalVelocity_ += kGravity * dt;
  const glm::vec3 verticalDelta(0.0f, verticalVelocity_ * dt, 0.0f);
  const glm::vec3 resolved = world->ResolveMovement(eyePos, verticalDelta, cap);
  const float movedY = resolved.y - eyePos.y;
  const float requestedY = verticalDelta.y;
  const bool hitFloor = std::abs(movedY - requestedY) > 1e-4f && requestedY < 0.0f;
  eyePos = resolved;
  if (hitFloor) {
   landStanding(world, eyePos);
  } else {
   feetAnchored_ = false;
  }
 }

 spaceWasPressed_ = input.jumpHeld;
 updateLocomotionState(input);
}

} // namespace cutum
