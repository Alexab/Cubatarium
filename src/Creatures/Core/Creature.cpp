#include "Creatures/Environment/CreatureEnvironment.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureBounds.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include "Creatures/Player/PlayerCapsule.h"
#include "Creatures/Visual/CreaturePartMeshData.h"
#include "Creatures/Visual/CreatureVisual.h"
#include "World/Core/World.h"
#include "World/Math/GridMath.h"
#include <cmath>
#include <cstdlib>
#include <optional>

namespace cutum
{

UCreature::UCreature(CreatureId Id, std::string typeId, glm::vec3 bodyOrigin,
                     glm::vec3 eyeOffset)
    : Id(Id), TypeId(std::move(typeId)), BodyOrigin(bodyOrigin),
      EyeOffset(eyeOffset)
{
  Bounds.profile.restSizeBlocks = glm::vec3(0.6f, 1.8f, 0.6f);
  Bounds.profile.minSizeBlocks = glm::vec3(0.6f, 1.5f, 0.6f);
  Bounds.profile.maxSizeBlocks = glm::vec3(0.6f, 1.8f, 0.6f);
  Bounds.currentSizeBlocks = Bounds.profile.restSizeBlocks;
  Locomotion.Reset();
  LastBodyOrigin = BodyOrigin;
}

UCreature::~UCreature() = default;

void UCreature::SetVisual(std::unique_ptr<ICreatureVisual> visual)
{
  Visual = std::move(visual);
}

glm::vec3 UCreature::GetEyePosition() const { return GetLocomotionEye(); }

glm::vec3 UCreature::GetLocomotionEye() const
{
  return glm::vec3(BodyOrigin.x, BodyOrigin.y + Locomotion.GetViewEyeHeight(),
                   BodyOrigin.z);
}

void UCreature::SetOrientation(float yaw, float pitch)
{
  Yaw = yaw;
  Pitch = pitch;
}

void UCreature::SyncBoundsFromStance()
{
  Bounds = LerpBoundsStance(Bounds, Locomotion.GetStanceBlend());
  Locomotion.SetCollisionProfile(Bounds.profile.restSizeBlocks, EyeOffset.y);
}

CollisionVolume UCreature::GetCollisionVolume() const
{
  return CollisionVolumeFromBody(BodyOrigin, Bounds.profile.restSizeBlocks);
}

void UCreature::SyncFeetFromLocomotion(const UWorld &world,
                                       glm::vec3 &eyeAfterLocomotion)
{
  BodyOrigin.x = eyeAfterLocomotion.x;
  BodyOrigin.z = eyeAfterLocomotion.z;

  const bool useGround =
      Locomotion.GetMode() == CreatureMovementMode::Walking &&
      Locomotion.IsFeetAnchored() && Locomotion.IsOnGround();
  if (useGround)
  {
    const float refFeetY = FeetYFromEye(eyeAfterLocomotion, EyeOffset.y);
    const int gx = WorldCoordToBlockIndex(BodyOrigin.x);
    const int gz = WorldCoordToBlockIndex(BodyOrigin.z);
    const PlayerCapsule cap = PlayerCapsule::FromCreatureBlocks(
        Bounds.currentSizeBlocks, EyeOffset.y);
    if (const std::optional<float> groundY =
            world.QueryGroundFeetYUnder(gx, gz, refFeetY))
    {
      BodyOrigin.y = *groundY;
      eyeAfterLocomotion.y = BodyOrigin.y + Locomotion.GetViewEyeHeight();
      Locomotion.SyncFeetAnchorFromView(*groundY, true);
      return;
    }
  }

  BodyOrigin.y = FeetYFromEye(eyeAfterLocomotion, EyeOffset.y);
  if (Locomotion.IsFeetAnchored())
  {
    Locomotion.SyncFeetAnchorFromView(BodyOrigin.y, true);
  }
}

void UCreature::RebuildLocomotionFacts(
    const CreatureLocomotionRawInput &input,
    const CreatureLocomotionCapabilities &caps, const UWorld *world)
{
  const float prevPhase = LocomotionFacts.animPhase;
  CreatureLocomotionFacts raw;
  FillTerrestrialRawFacts(raw, input, LocomotionArchetype, Yaw, Pitch);
  if (LocomotionArchetype == LocomotionArchetype::Aquatic && input.dt > 1e-6f)
  {
    const glm::vec3 delta = input.bodyOriginAfter - input.bodyOriginBefore;
    raw.horizontalSpeed = glm::length(delta) / input.dt;
  }
  raw.animPhase = prevPhase;
  if (Intent.lookAtWeight > 0.0f)
  {
    raw.lookAtWorld = Intent.lookAtWorld;
    raw.lookAtWeight = Intent.lookAtWeight;
  }
  if (world)
  {
    ApplyEnvironmentLocomotionFacts(*world, BodyOrigin,
                                    Bounds.profile.restSizeBlocks, raw);
  }
  LocomotionFacts = raw;
  CreatureLocomotionRawInput deriveInput = input;
  if (Intent.suggestedAnim != LocomotionState::Idle)
  {
    deriveInput.suggestedAnim = Intent.suggestedAnim;
    deriveInput.hasSuggestedAnim = true;
  }
  FinalizeLocomotionFacts(LocomotionFacts, caps, deriveInput, WalkCycleHz,
                          input.dt);
}

void UCreature::RebuildLocomotionFactsFromController(
    const UCreatureLocomotionController &controller,
    const CreatureLocomotionCapabilities &caps, float dt,
    float horizontalSpeedOverride, const UWorld *world)
{
  const float prevPhase = LocomotionFacts.animPhase;
  CreatureLocomotionRawInput input;
  input.locomotion = &controller;
  input.bodyOriginBefore = BodyOrigin;
  input.bodyOriginAfter = BodyOrigin;
  input.dt = dt;
  CreatureLocomotionFacts raw;
  FillTerrestrialRawFacts(raw, input, LocomotionArchetype, Yaw, Pitch);
  raw.animPhase = prevPhase;
  if (horizontalSpeedOverride >= 0.0f)
  {
    raw.horizontalSpeed = horizontalSpeedOverride;
  }
  if (Intent.lookAtWeight > 0.0f)
  {
    raw.lookAtWorld = Intent.lookAtWorld;
    raw.lookAtWeight = Intent.lookAtWeight;
  }
  if (world)
  {
    ApplyEnvironmentLocomotionFacts(*world, BodyOrigin,
                                    Bounds.profile.restSizeBlocks, raw);
  }
  LocomotionFacts = raw;
  FinalizeLocomotionFacts(LocomotionFacts, caps, input, WalkCycleHz, dt);
}

void UCreature::ExecuteIntent(UWorld &world, float dt)
{
  const glm::vec3 bodyOriginBefore = BodyOrigin;

  if (!Possessed && Locomotion.GetMode() == CreatureMovementMode::Walking)
  {
    glm::vec3 eye = GetLocomotionEye();
    CreatureInput emptyInput;
    Locomotion.UpdateLocomotion(&world, eye, emptyInput, dt, Id);
    SyncFeetFromLocomotion(world, eye);
    SyncBoundsFromStance();
  }

  if (Possessed)
  {
    if (Intent.clearOnApply)
    {
      ClearIntent();
    }
    return;
  }

  if (Intent.moveDirWorld != glm::vec3(0.0f))
  {
    const CreatureDefinition *def = world.GetCreatureDefinition(TypeId);
    const CreatureHabitat habitat =
        def ? def->habitat : CreatureHabitat::Terrestrial;
    glm::vec3 delta = Intent.moveDirWorld * Intent.moveSpeed * dt;
    if (habitat == CreatureHabitat::Terrestrial)
    {
      delta.y = 0.0f;
    }
    else if (habitat == CreatureHabitat::Amphibious)
    {
      const EnvironmentSample env = ProbeEnvironmentAt(
          world, BodyOrigin, Bounds.profile.restSizeBlocks);
      if (!env.inWater)
      {
        delta.y = 0.0f;
      }
    }
    const glm::vec3 candidate = world.ResolveMovementBody(
        BodyOrigin, delta, Bounds.profile.restSizeBlocks, Id);
    if (CanCreatureOccupyAt(world, habitat, candidate,
                            Bounds.profile.restSizeBlocks))
    {
      BodyOrigin = candidate;
    }
  }

  if (!Possessed)
  {
    const CreatureDefinition *def = world.GetCreatureDefinition(TypeId);
    const CreatureHabitat habitat =
        def ? def->habitat : CreatureHabitat::Terrestrial;
    if (habitat == CreatureHabitat::Aquatic ||
        habitat == CreatureHabitat::Amphibious ||
        habitat == CreatureHabitat::Lava)
    {
      const CollisionVolume vol = GetCollisionVolume();
      const UWorld::SampledFluidState fluid =
          world.SampleFluidPhysicsVolume(vol);
      if (fluid.inFluid && glm::length(Intent.moveDirWorld) < 1e-4f)
      {
        const float sink = fluid.SinkSpeed * dt * 0.2f;
        const glm::vec3 buoyancyDelta(0.0f, -sink, 0.0f);
        BodyOrigin = world.ResolveMovementBody(
            BodyOrigin, buoyancyDelta, Bounds.profile.restSizeBlocks, Id);
      }
    }
  }

  if (!Possessed)
  {
    glm::vec2 faceDir(Intent.moveDirWorld.x, Intent.moveDirWorld.z);
    const glm::vec3 xzDelta = BodyOrigin - bodyOriginBefore;
    const glm::vec2 xzActual(xzDelta.x, xzDelta.z);
    if (glm::length(xzActual) > 1e-5f)
    {
      faceDir = xzActual;
    }
    if (glm::length(faceDir) > 1e-4f)
    {
      Yaw = ModelYawFromDirection(faceDir.x, faceDir.y) + ModelYawOffsetDeg;
      if (Yaw > 180.0f)
      {
        Yaw -= 360.0f;
      }
      else if (Yaw <= -180.0f)
      {
        Yaw += 360.0f;
      }
      Pitch = 0.0f;
    }
  }

  if (Intent.clearOnApply)
  {
    ClearIntent();
  }

  CreatureLocomotionRawInput rawInput;
  rawInput.locomotion = &Locomotion;
  rawInput.bodyOriginBefore = bodyOriginBefore;
  rawInput.bodyOriginAfter = BodyOrigin;
  rawInput.dt = dt;
  RebuildLocomotionFacts(rawInput, Locomotion.GetCapabilities(), &world);
  LastBodyOrigin = BodyOrigin;
}

void UCreature::UpdateControlled(UWorld &world, const CreatureInput &input,
                                 float dt)
{
  ClearIntent();
  glm::vec3 eye = GetLocomotionEye();
  Locomotion.UpdateLocomotion(&world, eye, input, dt, Id);
  SyncFeetFromLocomotion(world, eye);
  SyncBoundsFromStance();
}

} // namespace cutum
