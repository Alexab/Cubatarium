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

/// Global move-speed multiplier for external harnesses (flight-sim far
/// distance stress). Production default 1; the App layer wires its harness
/// env var into this at startup — locomotion itself must not include
/// App/Platform (audit M7).
float &CreatureMoveSpeedScaleOverride();

class UCreatureLocomotionController
{
public:
  void Reset();
  void SetCapabilities(const CreatureLocomotionCapabilities &caps);
  void SetCollisionProfile(const glm::vec3 &sizeBlocks, float eyeHeight);

  float GetWalkSpeed() const { return Caps.walkSpeed; }
  float GetFlySpeed() const
  {
    return Caps.walkSpeed * Caps.flySpeedMultiplier;
  }
  const CreatureLocomotionCapabilities &GetCapabilities() const { return Caps; }

  float ResolveHorizontalSpeed(const CreatureInput &input) const;
  bool IsSprinting(const CreatureInput &input) const;

  CreatureMovementMode GetMode() const { return Mode; }
  void SetMode(CreatureMovementMode mode);

  bool IsOnGround() const { return OnGround; }
  /// Rider mount support (runtime issue 5): set by the world each frame
  /// when the player stands on a rideable creature; the back Y acts as the
  /// ground plane for anchoring.
  void SetMountSupport(CreatureId mountId, float backY)
  {
    MountSupportId = mountId;
    MountSupportBackY = backY;
  }
  void ClearMountSupport()
  {
    MountSupportId = 0;
    MountSupportBackY = 0.f;
  }
  CreatureId GetMountSupportId() const { return MountSupportId; }
  float GetMountSupportBackY() const { return MountSupportBackY; }
  /// External ground-invalidation hook: the caller verified the support
  /// vanished (e.g. blocks dug out under an idle body) — drop the grounded
  /// state so the next motor step re-engages gravity.
  void BreakGroundSupport()
  {
    OnGround = false;
    FeetAnchored = false;
  }
  float GetStanceBlend() const { return StanceBlend; }
  float GetVerticalVelocity() const { return VerticalVelocity; }
  void ClampVerticalVelocity(float minValue);
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
  CreatureId MountSupportId{0};
  float MountSupportBackY{0.f};

  // UpdateLocomotion mode branches (audit 4.2 tail).
  void UpdateFlight(const UWorld *world, glm::vec3 &eyePos,
                    const CreatureInput &input);
  void UpdateSwimming(const UWorld *world, glm::vec3 &eyePos,
                      const CreatureInput &input, float dt,
                      const struct SampledFluidState &fluid,
                      const PlayerCapsule &cap, CreatureId skipCreatureId);
  void updateLocomotionState(const CreatureInput &input);
  void SyncMountedPose(const UWorld *world, glm::vec3 &eyePos,
                       const CreatureInput &input, float dt,
                       CreatureId skipCreatureId);
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
  float lerpEyeHeightForStance(float blend01) const;

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
