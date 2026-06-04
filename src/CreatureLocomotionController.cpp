#include "CreatureLocomotionController.h"
#include "CreatureBounds.h"
#include "GridMath.h"
#include "World.h"
#include <algorithm>
#include <cmath>

namespace cutum {

namespace {

CreatureLocomotionCapabilities ClampCapabilities(CreatureLocomotionCapabilities caps)
{
 caps.jumpHeightBlocks = std::clamp(caps.jumpHeightBlocks, 0.25f, 3.0f);
 caps.walkSpeed = std::clamp(caps.walkSpeed, 0.5f, 20.0f);
 caps.flySpeed = std::clamp(caps.flySpeed, 0.5f, 20.0f);
 return caps;
}

} // namespace

void CreatureLocomotionController::SetCapabilities(const CreatureLocomotionCapabilities& caps)
{
 caps_ = ClampCapabilities(caps);
 jumpSpeed_ = caps_.canJump ? JumpSpeedFromHeight(caps_.jumpHeightBlocks, kGravityMagnitude) : 0.0f;
}

void CreatureLocomotionController::SetCollisionProfile(const glm::vec3& sizeBlocks, float eyeHeight)
{
 collisionSizeBlocks_ = sizeBlocks;
 eyeHeight_ = eyeHeight;
}

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
 return PlayerCapsule::FromCreatureBlocks(collisionSizeBlocks_, eyeHeight_);
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
 landStanding(world, eyePos, world ? world->GetMovementCollisionSkipId() : 0);
 spaceWasPressed_ = true;
 suppressNextJump_ = false;
}

void CreatureLocomotionController::SyncAfterStepLanding(glm::vec3& eyePos, const World* world)
{
 onGround_ = true;
 verticalVelocity_ = 0.0f;
 if (world) {
  anchorFeetFromStandingEye(world, eyePos);
  syncEyeFromFeet(eyePos);
 }
 updateLocomotionState({});
}

bool CreatureLocomotionController::anchorFeetFromStandingEye(const World* world,
                                                             const glm::vec3& eyePos)
{
 if (!world) {
  return false;
 }
 const PlayerCapsule cap = GetCapsule();
 if (!world->HasGroundSupport(eyePos, cap)) {
  feetAnchored_ = false;
  return false;
 }
 feetY_ = cap.feetY(eyePos);
 feetAnchored_ = true;
 return true;
}

float CreatureLocomotionController::GetViewEyeHeight() const
{
 const PlayerCapsule stand = GetCapsule();
 const float crouchEye = eyeHeight_ * 0.85f;
 const float t = std::clamp(stanceBlend_, 0.0f, 1.0f);
 return stand.eyeHeight + (crouchEye - stand.eyeHeight) * t;
}

void CreatureLocomotionController::SetStanceBlendForView(float blend01)
{
 stanceBlend_ = std::clamp(blend01, 0.0f, 1.0f);
}

void CreatureLocomotionController::SyncFeetAnchorFromView(float feetY, bool anchored)
{
 feetY_ = feetY;
 feetAnchored_ = anchored;
}

void CreatureLocomotionController::syncEyeFromFeet(glm::vec3& eyePos) const
{
 if (!feetAnchored_) {
  return;
 }
 eyePos.y = feetY_ + GetViewEyeHeight();
}

void CreatureLocomotionController::landStanding(const World* world, glm::vec3& eyePos,
                                                CreatureId skipCreatureId)
{
 stanceBlend_ = 0.0f;
 if (!world) {
  onGround_ = false;
  feetAnchored_ = false;
  return;
 }
 if (!anchorFeetFromStandingEye(world, eyePos)) {
  onGround_ = false;
  return;
 }
 const PlayerCapsule cap = GetCapsule();
 if (!world->IsValidStandFootprint(eyePos, cap, feetY_)) {
  onGround_ = false;
  feetAnchored_ = false;
  return;
 }
 eyePos.y = feetY_ + cap.eyeHeight;
 if (world->CheckCollision(eyePos, cap, skipCreatureId)) {
  onGround_ = false;
  feetAnchored_ = false;
  return;
 }

 onGround_ = true;
 verticalVelocity_ = 0.0f;
 syncEyeFromFeet(eyePos);
}

bool CreatureLocomotionController::canStandUpAt(const World* world,
                                                 const glm::vec3& eyePos,
                                                 CreatureId skipCreatureId) const
{
 if (!world || !feetAnchored_) {
  return true;
 }
 const PlayerCapsule cap = GetCapsule();
 const glm::vec3 trialEye(eyePos.x, feetY_ + GetViewEyeHeight(), eyePos.z);
 return !world->CheckCollision(trialEye, GetCapsule(), skipCreatureId);
}

void CreatureLocomotionController::updateStanceBlend(const World* world, const glm::vec3& eyePos,
                                                     const CreatureInput& input, float dt,
                                                     CreatureId skipCreatureId)
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
 if (target < 1.0f && stanceBlend_ > 0.0f && !canStandUpAt(world, eyePos, skipCreatureId)) {
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
                                                      const CreatureInput& input,
                                                      CreatureId skipCreatureId)
{
 if (stanceBlend_ <= kJumpStanceMax || !onGround_ || !feetAnchored_) {
  return false;
 }
 if (!canStandUpAt(world, eyePos, skipCreatureId)) {
  return false;
 }
 stanceBlend_ = 0.0f;
 syncEyeFromFeet(eyePos);
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
 verticalVelocity_ = jumpSpeed_;
 return true;
}

void CreatureLocomotionController::syncGroundedPose(const World* world, glm::vec3& eyePos,
                                                    const CreatureInput& input, float dt,
                                                    CreatureId skipCreatureId)
{
 onGround_ = true;
 verticalVelocity_ = 0.0f;
 updateStanceBlend(world, eyePos, input, dt, skipCreatureId);

 if (stanceBlend_ > kJumpStanceMax) {
  if (!feetAnchored_) {
   anchorFeetFromStandingEye(world, eyePos);
  }
 } else {
  anchorFeetFromStandingEye(world, eyePos);
 }
 syncEyeFromFeet(eyePos);
}

void CreatureLocomotionController::UpdateLocomotion(const World* world, glm::vec3& eyePos,
                                                   const CreatureInput& input, float dt,
                                                   CreatureId skipCreatureId)
{
 if (!world) {
  return;
 }

 if (mode_ == CreatureMovementMode::Flying) {
  stanceBlend_ = 0.0f;
  const PlayerCapsule cap = GetCapsule();
  if (world->HasGroundSupport(eyePos, cap)) {
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
  if (stanceBlend_ > kJumpStanceMax && canStandUpAt(world, eyePos, skipCreatureId)) {
   if (tryStandFromCrouch(world, eyePos, input, skipCreatureId)) {
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
  eyePos = world->ResolveMovement(eyePos, verticalDelta, cap, skipCreatureId);
  updateStanceBlend(world, eyePos, input, dt, skipCreatureId);
  if (stanceBlend_ > kJumpStanceMax && feetAnchored_) {
   syncEyeFromFeet(eyePos);
  }
  spaceWasPressed_ = input.jumpHeld;
  updateLocomotionState(input);
  return;
 }

 const CollisionVolume supportVol = CollisionVolumeFromEye(eyePos, cap);
 const float feetY = supportVol.center.y - supportVol.halfExtents.y;
 const bool supported = world->HasGroundSupportVolume(supportVol, feetY);
 const bool groundedPose = verticalVelocity_ <= 0.05f && supported;

 if (groundedPose) {
  syncGroundedPose(world, eyePos, input, dt, skipCreatureId);
 } else {
  onGround_ = false;
  updateStanceBlend(world, eyePos, input, dt, skipCreatureId);

  verticalVelocity_ += kGravity * dt;
  const glm::vec3 verticalDelta(0.0f, verticalVelocity_ * dt, 0.0f);
  const glm::vec3 resolved = world->ResolveMovement(eyePos, verticalDelta, cap, skipCreatureId);
  const float movedY = resolved.y - eyePos.y;
  const float requestedY = verticalDelta.y;
  const bool hitFloor = std::abs(movedY - requestedY) > 1e-4f && requestedY < 0.0f;
  eyePos = resolved;
  if (hitFloor) {
   landStanding(world, eyePos, skipCreatureId);
  } else {
   feetAnchored_ = false;
  }
 }

 spaceWasPressed_ = input.jumpHeld;
 updateLocomotionState(input);
}

} // namespace cutum
