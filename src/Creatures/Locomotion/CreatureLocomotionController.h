#ifndef CREATURELOCOMOTIONCONTROLLER_H
#define CREATURELOCOMOTIONCONTROLLER_H

#include "Creatures/Locomotion/LocomotionTypes.h"
#include "Creatures/Player/PlayerCapsule.h"
#include <chrono>
#include <cstdint>
#include <glm/glm.hpp>

namespace cutum
{

using CreatureId = uint64_t;

class UWorld;

class UCreatureLocomotionController
{
public:
  void Reset();
  void SetCapabilities(const CreatureLocomotionCapabilities &caps);
  void SetCollisionProfile(const glm::vec3 &sizeBlocks, float eyeHeight);

  float GetWalkSpeed() const { return Caps.walkSpeed; }
  float GetFlySpeed() const { return Caps.flySpeed; }
  const CreatureLocomotionCapabilities &GetCapabilities() const { return Caps; }

  CreatureMovementMode GetMode() const { return Mode; }
  void SetMode(CreatureMovementMode mode);

  bool IsOnGround() const { return OnGround; }
  float GetStanceBlend() const { return StanceBlend; }
  float GetVerticalVelocity() const { return VerticalVelocity; }
  LocomotionState GetLocomotionState() const { return LocomotionState; }
  PlayerCapsule GetCapsule() const;
  PlayerCapsule GetCollisionCapsule() const;
  float GetFeetY() const { return FeetY; }
  bool IsFeetAnchored() const { return FeetAnchored; }
  float GetViewEyeHeight() const;
  void SetStanceBlendForView(float blend01);
  void SyncFeetAnchorFromView(float feetY, bool anchored);

  bool OnSpacePressed();
  void OnLandedFromFlight(const UWorld *world, glm::vec3 &eyePos,
                          bool clearShiftKeys);
  void UpdateLocomotion(const UWorld *world, glm::vec3 &eyePos,
                        const CreatureInput &input, float dt,
                        CreatureId skipCreatureId = 0);

  bool ShouldBlockJump() const { return SuppressNextJump; }
  void NotifySpaceReleased() { SuppressNextJump = false; }
  void SyncAfterStepLanding(glm::vec3 &eyePos, const UWorld *world);
  bool ConsumeClearShiftRequest();

private:
  void updateLocomotionState(const CreatureInput &input);
  bool anchorFeetFromStandingEye(const UWorld *world, const glm::vec3 &eyePos);
  void syncEyeFromFeet(glm::vec3 &eyePos) const;
  void landStanding(const UWorld *world, glm::vec3 &eyePos,
                    CreatureId skipCreatureId);
  bool canStandUpAt(const UWorld *world, const glm::vec3 &eyePos,
                    CreatureId skipCreatureId) const;
  void updateStanceBlend(const UWorld *world, const glm::vec3 &eyePos,
                         const CreatureInput &input, float dt,
                         CreatureId skipCreatureId);
  bool tryJump(const UWorld *world, glm::vec3 &eyePos, const CreatureInput &input,
               CreatureId skipCreatureId);
  bool tryStandFromCrouch(const UWorld *world, glm::vec3 &eyePos,
                          const CreatureInput &input,
                          CreatureId skipCreatureId);
  void syncGroundedPose(const UWorld *world, glm::vec3 &eyePos,
                        const CreatureInput &input, float dt,
                        CreatureId skipCreatureId);

  CreatureMovementMode Mode{CreatureMovementMode::Walking};
  LocomotionState LocomotionState{LocomotionState::Idle};
  CreatureLocomotionCapabilities Caps{};
  float JumpSpeed{0.0f};
  glm::vec3 CollisionSizeBlocks{0.6f, 1.8f, 0.6f};
  float EyeHeight{1.62f};
  float StanceBlend{0.0f};
  float FeetY{0.0f};
  bool FeetAnchored{false};
  bool OnGround{false};
  float VerticalVelocity{0.0f};
  bool SpaceWasPressed{false};
  bool SuppressNextJump{false};
  bool ClearShiftRequest{false};
  int UnsupportedFluidStreak{0};
  std::chrono::steady_clock::time_point LastSpacePressTime{};

  static constexpr int kDoubleSpaceTapMs = 350;
  static constexpr float kGravity = -20.0f;
  static constexpr float kGravityMagnitude = 20.0f;
  static constexpr float kStanceTransitionDuration = 0.12f;
  static constexpr float kJumpStanceMax = 0.05f;
};

} // namespace cutum

#endif
