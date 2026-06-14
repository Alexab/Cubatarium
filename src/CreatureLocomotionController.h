#ifndef CREATURELOCOMOTIONCONTROLLER_H
#define CREATURELOCOMOTIONCONTROLLER_H

#include <chrono>
#include <glm/glm.hpp>
#include "LocomotionTypes.h"
#include <cstdint>
#include "PlayerCapsule.h"

namespace cutum {

using CreatureId = uint64_t;

class UWorld;

class UCreatureLocomotionController {
public:
 void Reset();
 void SetCapabilities(const CreatureLocomotionCapabilities& caps);
 void SetCollisionProfile(const glm::vec3& sizeBlocks, float eyeHeight);

 float GetWalkSpeed() const { return caps_.walkSpeed; }
 float GetFlySpeed() const { return caps_.flySpeed; }
 const CreatureLocomotionCapabilities& GetCapabilities() const { return caps_; }

 CreatureMovementMode GetMode() const { return mode_; }
 void SetMode(CreatureMovementMode mode);

 bool IsOnGround() const { return onGround_; }
 float GetStanceBlend() const { return stanceBlend_; }
 float GetVerticalVelocity() const { return verticalVelocity_; }
 LocomotionState GetLocomotionState() const { return locomotionState_; }
 PlayerCapsule GetCapsule() const;
 float GetFeetY() const { return feetY_; }
 bool IsFeetAnchored() const { return feetAnchored_; }
 float GetViewEyeHeight() const;
 void SetStanceBlendForView(float blend01);
 void SyncFeetAnchorFromView(float feetY, bool anchored);

 bool OnSpacePressed();
 void OnLandedFromFlight(const UWorld* world, glm::vec3& eyePos, bool clearShiftKeys);
 void UpdateLocomotion(const UWorld* world, glm::vec3& eyePos, const CreatureInput& input, float dt,
                       CreatureId skipCreatureId = 0);

 bool ShouldBlockJump() const { return suppressNextJump_; }
 void NotifySpaceReleased() { suppressNextJump_ = false; }
 void SyncAfterStepLanding(glm::vec3& eyePos, const UWorld* world);
 bool ConsumeClearShiftRequest();

private:
 void updateLocomotionState(const CreatureInput& input);
 bool anchorFeetFromStandingEye(const UWorld* world, const glm::vec3& eyePos);
 void syncEyeFromFeet(glm::vec3& eyePos) const;
 void landStanding(const UWorld* world, glm::vec3& eyePos, CreatureId skipCreatureId);
 bool canStandUpAt(const UWorld* world, const glm::vec3& eyePos, CreatureId skipCreatureId) const;
 void updateStanceBlend(const UWorld* world, const glm::vec3& eyePos, const CreatureInput& input,
                        float dt, CreatureId skipCreatureId);
 bool tryJump(const CreatureInput& input);
 bool tryStandFromCrouch(const UWorld* world, glm::vec3& eyePos, const CreatureInput& input,
                         CreatureId skipCreatureId);
 void syncGroundedPose(const UWorld* world, glm::vec3& eyePos, const CreatureInput& input,
                       float dt, CreatureId skipCreatureId);

 CreatureMovementMode mode_{CreatureMovementMode::Walking};
 LocomotionState locomotionState_{LocomotionState::Idle};
 CreatureLocomotionCapabilities caps_{};
 float jumpSpeed_{0.0f};
 glm::vec3 collisionSizeBlocks_{0.6f, 1.8f, 0.6f};
 float eyeHeight_{1.62f};
 float stanceBlend_{0.0f};
 float feetY_{0.0f};
 bool feetAnchored_{false};
 bool onGround_{false};
 float verticalVelocity_{0.0f};
 bool spaceWasPressed_{false};
 bool suppressNextJump_{false};
 bool clearShiftRequest_{false};
 std::chrono::steady_clock::time_point lastSpacePressTime_{};

 static constexpr int kDoubleSpaceTapMs = 350;
 static constexpr float kGravity = -20.0f;
 static constexpr float kGravityMagnitude = 20.0f;
 static constexpr float kStanceTransitionDuration = 0.12f;
 static constexpr float kJumpStanceMax = 0.05f;
};

} // namespace cutum

#endif
