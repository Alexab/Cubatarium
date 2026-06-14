#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureBounds.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include "Creatures/Visual/CreaturePartMeshData.h"
#include "Creatures/Visual/CreatureVisual.h"
#include "World/Math/GridMath.h"
#include "Creatures/Player/PlayerCapsule.h"
#include "World/Core/World.h"
#include <cmath>
#include <cstdlib>
#include <optional>

namespace cutum
{

UCreature::UCreature(CreatureId id, std::string typeId, glm::vec3 bodyOrigin,
                     glm::vec3 eyeOffset)
    : Id(id), TypeId(std::move(typeId)), BodyOrigin(bodyOrigin),
      EyeOffset(eyeOffset)
{
  bounds_.profile.restSizeBlocks = glm::vec3(0.6f, 1.8f, 0.6f);
  bounds_.profile.minSizeBlocks = glm::vec3(0.6f, 1.5f, 0.6f);
  bounds_.profile.maxSizeBlocks = glm::vec3(0.6f, 1.8f, 0.6f);
  bounds_.currentSizeBlocks = bounds_.profile.restSizeBlocks;
  locomotion_.Reset();
  lastBodyOrigin_ = BodyOrigin;
}

UCreature::~UCreature() = default;

void UCreature::SetVisual(std::unique_ptr<ICreatureVisual> visual)
{
  visual_ = std::move(visual);
}

glm::vec3 UCreature::GetEyePosition() const { return GetLocomotionEye(); }

glm::vec3 UCreature::GetLocomotionEye() const
{
  return glm::vec3(BodyOrigin.x, BodyOrigin.y + locomotion_.GetViewEyeHeight(),
                   BodyOrigin.z);
}

void UCreature::SetOrientation(float yaw, float pitch)
{
  yaw_ = yaw;
  pitch_ = pitch;
}

void UCreature::SyncBoundsFromStance()
{
  bounds_ = LerpBoundsStance(bounds_, locomotion_.GetStanceBlend());
  locomotion_.SetCollisionProfile(bounds_.profile.restSizeBlocks, EyeOffset.y);
}

CollisionVolume UCreature::GetCollisionVolume() const
{
  return CollisionVolumeFromBody(BodyOrigin, bounds_.profile.restSizeBlocks);
}

void UCreature::SyncFeetFromLocomotion(const UWorld &world,
                                       glm::vec3 &eyeAfterLocomotion)
{
  BodyOrigin.x = eyeAfterLocomotion.x;
  BodyOrigin.z = eyeAfterLocomotion.z;

  const bool useGround =
      locomotion_.GetMode() == CreatureMovementMode::Walking &&
      locomotion_.IsFeetAnchored() && locomotion_.IsOnGround();
  if (useGround)
  {
    const float refFeetY = FeetYFromEye(eyeAfterLocomotion, EyeOffset.y);
    const int gx = WorldCoordToBlockIndex(BodyOrigin.x);
    const int gz = WorldCoordToBlockIndex(BodyOrigin.z);
    const PlayerCapsule cap = PlayerCapsule::FromCreatureBlocks(
        bounds_.currentSizeBlocks, EyeOffset.y);
    if (const std::optional<float> groundY =
            world.QueryGroundFeetYUnder(gx, gz, refFeetY))
    {
      BodyOrigin.y = *groundY;
      eyeAfterLocomotion.y = BodyOrigin.y + locomotion_.GetViewEyeHeight();
      locomotion_.SyncFeetAnchorFromView(*groundY, true);
      return;
    }
  }

  BodyOrigin.y = FeetYFromEye(eyeAfterLocomotion, EyeOffset.y);
  if (locomotion_.IsFeetAnchored())
  {
    locomotion_.SyncFeetAnchorFromView(BodyOrigin.y, true);
  }
}

void UCreature::RebuildLocomotionFacts(
    const CreatureLocomotionRawInput &input,
    const CreatureLocomotionCapabilities &caps)
{
  const float prevPhase = locomotionFacts_.animPhase;
  CreatureLocomotionFacts raw;
  FillTerrestrialRawFacts(raw, input, locomotionArchetype_, yaw_, pitch_);
  raw.animPhase = prevPhase;
  if (intent_.lookAtWeight > 0.0f)
  {
    raw.lookAtWorld = intent_.lookAtWorld;
    raw.lookAtWeight = intent_.lookAtWeight;
  }
  locomotionFacts_ = raw;
  CreatureLocomotionRawInput deriveInput = input;
  if (intent_.suggestedAnim != LocomotionState::Idle)
  {
    deriveInput.suggestedAnim = intent_.suggestedAnim;
    deriveInput.hasSuggestedAnim = true;
  }
  FinalizeLocomotionFacts(locomotionFacts_, caps, deriveInput, walkCycleHz_,
                          input.dt);
}

void UCreature::RebuildLocomotionFactsFromController(
    const UCreatureLocomotionController &controller,
    const CreatureLocomotionCapabilities &caps, float dt,
    float horizontalSpeedOverride)
{
  const float prevPhase = locomotionFacts_.animPhase;
  CreatureLocomotionRawInput input;
  input.locomotion = &controller;
  input.bodyOriginBefore = BodyOrigin;
  input.bodyOriginAfter = BodyOrigin;
  input.dt = dt;
  CreatureLocomotionFacts raw;
  FillTerrestrialRawFacts(raw, input, locomotionArchetype_, yaw_, pitch_);
  raw.animPhase = prevPhase;
  if (horizontalSpeedOverride >= 0.0f)
  {
    raw.horizontalSpeed = horizontalSpeedOverride;
  }
  if (intent_.lookAtWeight > 0.0f)
  {
    raw.lookAtWorld = intent_.lookAtWorld;
    raw.lookAtWeight = intent_.lookAtWeight;
  }
  locomotionFacts_ = raw;
  FinalizeLocomotionFacts(locomotionFacts_, caps, input, walkCycleHz_, dt);
}

void UCreature::ExecuteIntent(UWorld &world, float dt)
{
  const glm::vec3 bodyOriginBefore = BodyOrigin;

  if (!possessed_ && locomotion_.GetMode() == CreatureMovementMode::Walking)
  {
    glm::vec3 eye = GetLocomotionEye();
    CreatureInput emptyInput;
    locomotion_.UpdateLocomotion(&world, eye, emptyInput, dt, Id);
    SyncFeetFromLocomotion(world, eye);
    SyncBoundsFromStance();
  }

  if (possessed_)
  {
    if (intent_.clearOnApply)
    {
      ClearIntent();
    }
    return;
  }

  if (intent_.moveDirWorld != glm::vec3(0.0f))
  {
    glm::vec3 delta = intent_.moveDirWorld * intent_.moveSpeed * dt;
    delta.y = 0.0f;
    BodyOrigin = world.ResolveMovementBody(BodyOrigin, delta,
                                           bounds_.profile.restSizeBlocks, Id);
  }

  if (!possessed_)
  {
    glm::vec2 faceDir(intent_.moveDirWorld.x, intent_.moveDirWorld.z);
    const glm::vec3 xzDelta = BodyOrigin - bodyOriginBefore;
    const glm::vec2 xzActual(xzDelta.x, xzDelta.z);
    if (glm::length(xzActual) > 1e-5f)
    {
      faceDir = xzActual;
    }
    if (glm::length(faceDir) > 1e-4f)
    {
      yaw_ = ModelYawFromDirection(faceDir.x, faceDir.y) + modelYawOffsetDeg_;
      if (yaw_ > 180.0f)
      {
        yaw_ -= 360.0f;
      }
      else if (yaw_ <= -180.0f)
      {
        yaw_ += 360.0f;
      }
      pitch_ = 0.0f;
    }
  }

  if (intent_.clearOnApply)
  {
    ClearIntent();
  }

  CreatureLocomotionRawInput rawInput;
  rawInput.locomotion = &locomotion_;
  rawInput.bodyOriginBefore = bodyOriginBefore;
  rawInput.bodyOriginAfter = BodyOrigin;
  rawInput.dt = dt;
  RebuildLocomotionFacts(rawInput, locomotion_.GetCapabilities());
  lastBodyOrigin_ = BodyOrigin;
}

void UCreature::UpdateControlled(UWorld &world, const CreatureInput &input,
                                 float dt)
{
  ClearIntent();
  glm::vec3 eye = GetLocomotionEye();
  locomotion_.UpdateLocomotion(&world, eye, input, dt, Id);
  SyncFeetFromLocomotion(world, eye);
  SyncBoundsFromStance();
}

} // namespace cutum
