#include "Creature.h"
#include "CreatureDefinition.h"
#include "CreatureVisual.h"
#include "CreaturePartMeshData.h"
#include "World.h"
#include "CreatureBounds.h"
#include "GridMath.h"
#include "PlayerCapsule.h"
#include <cmath>
#include <cstdlib>
#include <optional>

namespace cutum {

Creature::Creature(CreatureId id, std::string typeId, glm::vec3 bodyOrigin, glm::vec3 eyeOffset)
    : id_(id)
    , typeId_(std::move(typeId))
    , bodyOrigin_(bodyOrigin)
    , eyeOffset_(eyeOffset)
{
 bounds_.profile.restSizeBlocks = glm::vec3(0.6f, 1.8f, 0.6f);
 bounds_.profile.minSizeBlocks = glm::vec3(0.6f, 1.5f, 0.6f);
 bounds_.profile.maxSizeBlocks = glm::vec3(0.6f, 1.8f, 0.6f);
 bounds_.currentSizeBlocks = bounds_.profile.restSizeBlocks;
 locomotion_.Reset();
 lastBodyOrigin_ = bodyOrigin_;
}

Creature::~Creature() = default;

void Creature::SetVisual(std::unique_ptr<ICreatureVisual> visual)
{
 visual_ = std::move(visual);
}

glm::vec3 Creature::GetEyePosition() const
{
 return GetLocomotionEye();
}

glm::vec3 Creature::GetLocomotionEye() const
{
 return glm::vec3(bodyOrigin_.x, bodyOrigin_.y + locomotion_.GetViewEyeHeight(), bodyOrigin_.z);
}

void Creature::SetOrientation(float yaw, float pitch)
{
 yaw_ = yaw;
 pitch_ = pitch;
}

void Creature::SyncBoundsFromStance()
{
 bounds_ = LerpBoundsStance(bounds_, locomotion_.GetStanceBlend());
 locomotion_.SetCollisionProfile(bounds_.profile.restSizeBlocks, eyeOffset_.y);
}

CollisionVolume Creature::GetCollisionVolume() const
{
 return CollisionVolumeFromBody(bodyOrigin_, bounds_.profile.restSizeBlocks);
}

void Creature::SyncFeetFromLocomotion(const World& world, glm::vec3& eyeAfterLocomotion)
{
 bodyOrigin_.x = eyeAfterLocomotion.x;
 bodyOrigin_.z = eyeAfterLocomotion.z;

 const bool useGround = locomotion_.GetMode() == CreatureMovementMode::Walking
                        && locomotion_.IsFeetAnchored() && locomotion_.IsOnGround();
 if (useGround) {
  const float refFeetY = FeetYFromEye(eyeAfterLocomotion, eyeOffset_.y);
  const int gx = WorldCoordToBlockIndex(bodyOrigin_.x);
  const int gz = WorldCoordToBlockIndex(bodyOrigin_.z);
  const PlayerCapsule cap =
      PlayerCapsule::FromCreatureBlocks(bounds_.currentSizeBlocks, eyeOffset_.y);
  if (const std::optional<float> groundY = world.QueryGroundFeetYUnder(gx, gz, refFeetY)) {
   bodyOrigin_.y = *groundY;
   eyeAfterLocomotion.y = bodyOrigin_.y + locomotion_.GetViewEyeHeight();
   locomotion_.SyncFeetAnchorFromView(*groundY, true);
   return;
  }
 }

 bodyOrigin_.y = FeetYFromEye(eyeAfterLocomotion, eyeOffset_.y);
 if (locomotion_.IsFeetAnchored()) {
  locomotion_.SyncFeetAnchorFromView(bodyOrigin_.y, true);
 }
}

void Creature::RebuildLocomotionFacts(const CreatureLocomotionRawInput& input,
                                    const CreatureLocomotionCapabilities& caps)
{
 const float prevPhase = locomotionFacts_.animPhase;
 CreatureLocomotionFacts raw;
 FillTerrestrialRawFacts(raw, input, locomotionArchetype_, yaw_, pitch_);
 raw.animPhase = prevPhase;
 if (intent_.lookAtWeight > 0.0f) {
  raw.lookAtWorld = intent_.lookAtWorld;
  raw.lookAtWeight = intent_.lookAtWeight;
 }
 locomotionFacts_ = raw;
 CreatureLocomotionRawInput deriveInput = input;
 if (intent_.suggestedAnim != LocomotionState::Idle) {
  deriveInput.suggestedAnim = intent_.suggestedAnim;
  deriveInput.hasSuggestedAnim = true;
 }
 FinalizeLocomotionFacts(locomotionFacts_, caps, deriveInput, walkCycleHz_, input.dt);
}

void Creature::RebuildLocomotionFactsFromController(const CreatureLocomotionController& controller,
                                                  const CreatureLocomotionCapabilities& caps,
                                                  float dt, float horizontalSpeedOverride)
{
 const float prevPhase = locomotionFacts_.animPhase;
 CreatureLocomotionRawInput input;
 input.locomotion = &controller;
 input.bodyOriginBefore = bodyOrigin_;
 input.bodyOriginAfter = bodyOrigin_;
 input.dt = dt;
 CreatureLocomotionFacts raw;
 FillTerrestrialRawFacts(raw, input, locomotionArchetype_, yaw_, pitch_);
 raw.animPhase = prevPhase;
 if (horizontalSpeedOverride >= 0.0f) {
  raw.horizontalSpeed = horizontalSpeedOverride;
 }
 if (intent_.lookAtWeight > 0.0f) {
  raw.lookAtWorld = intent_.lookAtWorld;
  raw.lookAtWeight = intent_.lookAtWeight;
 }
 locomotionFacts_ = raw;
 FinalizeLocomotionFacts(locomotionFacts_, caps, input, walkCycleHz_, dt);
}

void Creature::ExecuteIntent(World& world, float dt)
{
 const glm::vec3 bodyOriginBefore = bodyOrigin_;
 const float moveLen = glm::length(glm::vec2(intent_.moveDirWorld.x, intent_.moveDirWorld.z));
 if (!possessed_ && moveLen > 1e-4f) {
  yaw_ = ModelYawFromDirection(intent_.moveDirWorld.x, intent_.moveDirWorld.z);
  pitch_ = 0.0f;
 }

 if (!possessed_ && locomotion_.GetMode() == CreatureMovementMode::Walking) {
  glm::vec3 eye = GetLocomotionEye();
  CreatureInput emptyInput;
  locomotion_.UpdateLocomotion(&world, eye, emptyInput, dt, id_);
  SyncFeetFromLocomotion(world, eye);
  SyncBoundsFromStance();
 }

 if (possessed_) {
  if (intent_.clearOnApply) {
   ClearIntent();
  }
  return;
 }

 if (intent_.moveDirWorld != glm::vec3(0.0f)) {
  glm::vec3 delta = intent_.moveDirWorld * intent_.moveSpeed * dt;
  delta.y = 0.0f;
  bodyOrigin_ = world.ResolveMovementBody(bodyOrigin_, delta, bounds_.profile.restSizeBlocks, id_);
 }

 if (intent_.clearOnApply) {
  ClearIntent();
 }

 CreatureLocomotionRawInput rawInput;
 rawInput.locomotion = &locomotion_;
 rawInput.bodyOriginBefore = bodyOriginBefore;
 rawInput.bodyOriginAfter = bodyOrigin_;
 rawInput.dt = dt;
 RebuildLocomotionFacts(rawInput, locomotion_.GetCapabilities());
 lastBodyOrigin_ = bodyOrigin_;
}

void Creature::UpdateControlled(World& world, const CreatureInput& input, float dt)
{
 ClearIntent();
 glm::vec3 eye = GetLocomotionEye();
 locomotion_.UpdateLocomotion(&world, eye, input, dt, id_);
 SyncFeetFromLocomotion(world, eye);
 SyncBoundsFromStance();
}

} // namespace cutum
