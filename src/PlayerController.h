#ifndef PLAYERCONTROLLER_H
#define PLAYERCONTROLLER_H

#include <chrono>
#include <glm/glm.hpp>
#include "PlayerCapsule.h"

namespace cutum {

class World;

struct PlayerInput {
 bool moveForward{false};
 bool moveBack{false};
 bool moveLeft{false};
 bool moveRight{false};
 bool jumpHeld{false};
 bool jumpPressed{false};
 bool crouchHeld{false};
};

enum class PlayerMovementMode { Walking, Flying };

/// Locomotion state: walking / flying, stance blend, feet anchor, vertical physics.
class PlayerController {
public:
 void Reset();

 PlayerMovementMode GetMode() const { return mode_; }
 void SetMode(PlayerMovementMode mode);

 bool IsOnGround() const { return onGround_; }
 float GetStanceBlend() const { return stanceBlend_; }
 float GetVerticalVelocity() const { return verticalVelocity_; }
 PlayerCapsule GetCapsule() const;

 /// Double-tap Space flight toggle. Call once per physical Space press.
 bool OnSpacePressed();

 /// Ground contact while flying -> walking, standing.
 void OnLandedFromFlight(const World* world, glm::vec3& eyePos, bool clearShiftKeys);

 /// Walking / flying vertical + stance (after horizontal move). Updates eyePos.y and mode.
 void UpdateLocomotion(const World* world, glm::vec3& eyePos, const PlayerInput& input, float dt);

 bool ShouldBlockJump() const { return suppressNextJump_; }
 void NotifySpaceReleased() { suppressNextJump_ = false; }

 void SyncAfterStepLanding(glm::vec3& eyePos, const World* world);

 /// After standing from crouch with Space while Shift was held — clear Shift in Camera.
 bool ConsumeClearShiftRequest();

private:
 bool anchorFeetFromStandingEye(const World* world, const glm::vec3& eyePos);
 void applyCrouchEyeFromFeet(glm::vec3& eyePos) const;
 void landStanding(const World* world, glm::vec3& eyePos);
 bool canStandUpAt(const World* world, const glm::vec3& eyePos) const;
 void updateStanceBlend(const World* world, const glm::vec3& eyePos,
                      const PlayerInput& input, float dt);
 bool tryJump(const PlayerInput& input);
 bool tryStandFromCrouch(const World* world, glm::vec3& eyePos, const PlayerInput& input);
 void syncGroundedPose(const World* world, glm::vec3& eyePos, const PlayerInput& input, float dt);

 PlayerMovementMode mode_{PlayerMovementMode::Walking};
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
