#ifndef CREATURELOCOMOTIONCONTROLLER_H
#define CREATURELOCOMOTIONCONTROLLER_H

#include <chrono>
#include <glm/glm.hpp>
#include "LocomotionTypes.h"
#include "PlayerCapsule.h"

namespace cutum {

class World;

class CreatureLocomotionController {
public:
 void Reset();
 void SetCapabilities(const CreatureLocomotionCapabilities& caps) { caps_ = caps; }

 CreatureMovementMode GetMode() const { return mode_; }
 void SetMode(CreatureMovementMode mode);

 bool IsOnGround() const { return onGround_; }
 float GetStanceBlend() const { return stanceBlend_; }
 float GetVerticalVelocity() const { return verticalVelocity_; }
 LocomotionState GetLocomotionState() const { return locomotionState_; }
 PlayerCapsule GetCapsule() const;

 bool OnSpacePressed();
 void OnLandedFromFlight(const World* world, glm::vec3& eyePos, bool clearShiftKeys);
 void UpdateLocomotion(const World* world, glm::vec3& eyePos, const CreatureInput& input, float dt);

 bool ShouldBlockJump() const { return suppressNextJump_; }
 void NotifySpaceReleased() { suppressNextJump_ = false; }
 void SyncAfterStepLanding(glm::vec3& eyePos, const World* world);
 bool ConsumeClearShiftRequest();

private:
 void updateLocomotionState(const CreatureInput& input);
 bool anchorFeetFromStandingEye(const World* world, const glm::vec3& eyePos);
 void applyCrouchEyeFromFeet(glm::vec3& eyePos) const;
 void landStanding(const World* world, glm::vec3& eyePos);
 bool canStandUpAt(const World* world, const glm::vec3& eyePos) const;
 void updateStanceBlend(const World* world, const glm::vec3& eyePos, const CreatureInput& input,
                        float dt);
 bool tryJump(const CreatureInput& input);
 bool tryStandFromCrouch(const World* world, glm::vec3& eyePos, const CreatureInput& input);
 void syncGroundedPose(const World* world, glm::vec3& eyePos, const CreatureInput& input,
                       float dt);

 CreatureMovementMode mode_{CreatureMovementMode::Walking};
 LocomotionState locomotionState_{LocomotionState::Idle};
 CreatureLocomotionCapabilities caps_{};
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
 static constexpr float kJumpSpeed = 8.0f;
 static constexpr float kStanceTransitionDuration = 0.12f;
 static constexpr float kJumpStanceMax = 0.05f;
};

} // namespace cutum

#endif
