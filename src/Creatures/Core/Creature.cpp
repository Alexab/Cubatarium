#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureBounds.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include "Creatures/Environment/CreatureEnvironment.h"
#include "Creatures/Influence/StatusEffectSystem.h"
#include "Creatures/Locomotion/CreatureMotor.h"
#include "Creatures/Player/PlayerCapsule.h"
#include "Creatures/Visual/CreaturePartMeshData.h"
#include "Creatures/Visual/CreatureVisual.h"
#include "World/Core/World.h"
#include "World/Diagnostics/CreatureMovementDiagnostics.h"
#include "World/Math/GridMath.h"
#include <algorithm>
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

void UCreature::ApplyStatsFromDefinition(const CreatureDefinition &def)
{
  Vitals = def.stats.vitalsTemplate;
  Attributes = def.stats.attributes;
  NeedsTick = def.stats.needsTick;
  Attributes.ClampAll();
  Vitals.FillFull();
}

void UCreature::SetVisual(std::unique_ptr<IUCreatureVisual> visual)
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

  const bool walking =
      Locomotion.GetMode() == CreatureMovementMode::Walking;
  if (walking)
  {
    const float refFeetY = FeetYFromEye(eyeAfterLocomotion, EyeOffset.y);
    const int gx = WorldCoordToBlockIndex(BodyOrigin.x);
    const int gz = WorldCoordToBlockIndex(BodyOrigin.z);
    const float maxSnap =
        std::max(0.25f, Locomotion.GetCapabilities().jumpHeightBlocks);
    if (const std::optional<float> groundY =
            world.QueryGroundFeetYUnder(gx, gz, refFeetY))
    {
      const float drop = refFeetY - *groundY;
      const float climb = *groundY - refFeetY;
      // Snap when grounded, or heal brief unsupported / float states within
      // jumpHeight so habitat reject cannot trap a mob above air forever.
      const bool anchoredGround =
          Locomotion.IsFeetAnchored() && Locomotion.IsOnGround();
      if (anchoredGround || (drop >= 0.0f && drop <= maxSnap) ||
          (climb >= 0.0f && climb <= maxSnap))
      {
        BodyOrigin.y = *groundY;
        eyeAfterLocomotion.y = BodyOrigin.y + Locomotion.GetViewEyeHeight();
        Locomotion.SyncFeetAnchorFromView(*groundY, true);
        return;
      }
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
    // Amphibious: land uses terrestrial archetype; in fluid present as aquatic.
    if (raw.inFluid)
    {
      if (const CreatureDefinition *def = world->GetCreatureDefinition(TypeId))
      {
        if (def->habitat == CreatureHabitat::Amphibious)
        {
          raw.archetype = LocomotionArchetype::Aquatic;
          if (input.dt > 1e-6f)
          {
            const glm::vec3 delta =
                input.bodyOriginAfter - input.bodyOriginBefore;
            raw.horizontalSpeed = glm::length(delta) / input.dt;
          }
        }
      }
    }
  }
  LocomotionFacts = raw;
  CreatureLocomotionRawInput deriveInput = input;
  if (Intent.suggestedAnim != LocomotionState::Idle)
  {
    deriveInput.suggestedAnim = Intent.suggestedAnim;
    deriveInput.hasSuggestedAnim = true;
    deriveInput.intentMoveSpeed = Intent.moveSpeed;
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
  // Camera controller already resolved Walk/Run; speed-only derive can miss
  // sprint when multiplier sits near the run threshold.
  input.hasSuggestedAnim = true;
  input.suggestedAnim = controller.GetLocomotionState();
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
  const glm::vec3 diagIntentDir = Intent.moveDirWorld;
  const float diagIntentSpeed = Intent.moveSpeed;

  if (Possessed)
  {
    if (Intent.clearOnApply)
    {
      ClearIntent();
    }
    return;
  }

  const CreatureDefinition *def = world.GetCreatureDefinition(TypeId);
  const CreatureHabitat habitat =
      def ? def->habitat : CreatureHabitat::Terrestrial;
  const bool airMobility = habitat == CreatureHabitat::Aerial ||
                           Locomotion.GetMode() == CreatureMovementMode::Flying;

  // Cheap idle path: skip motor/habitat when grounded with zero wish.
  // Fluid sink / flight still need the full motor each frame.
  const bool idle_intent = glm::length(Intent.moveDirWorld) < 1e-4f &&
                           Intent.moveSpeed < 1e-4f && !Intent.wantJump;
  if (idle_intent && Locomotion.IsOnGround() && !airMobility &&
      habitat != CreatureHabitat::Aquatic &&
      habitat != CreatureHabitat::Lava && !LocomotionFacts.inFluid)
  {
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
    return;
  }

  if (habitat == CreatureHabitat::Aerial ||
      (Locomotion.GetCapabilities().canFly && airMobility))
  {
    Locomotion.SetMode(CreatureMovementMode::Flying);
  }
  else if (Locomotion.GetMode() == CreatureMovementMode::Flying &&
           habitat != CreatureHabitat::Aerial)
  {
    Locomotion.SetMode(CreatureMovementMode::Walking);
  }

  glm::vec3 wish = Intent.moveDirWorld;
  float speed = Intent.moveSpeed;
  {
    const float agi =
        0.75f + static_cast<float>(Attributes.agility) / 40.f; // ~1.0 at 10
    const float fatiguePenalty =
        Vitals.maxFatigue > 0.f
            ? (1.f - 0.4f * (Vitals.fatigue / Vitals.maxFatigue))
            : 1.f;
    speed *= agi * std::max(0.4f, fatiguePenalty);
  }
  speed *= StatusEffectSystem::GetMoveSpeedMultiplier(*this);
  if (habitat == CreatureHabitat::Terrestrial && !airMobility)
  {
    wish.y = 0.0f;
  }
  else if (habitat == CreatureHabitat::Amphibious && !airMobility)
  {
    const EnvironmentSample env =
        ProbeEnvironmentAt(world, BodyOrigin, Bounds.profile.restSizeBlocks);
    if (!env.inWater)
    {
      wish.y = 0.0f;
    }
  }

  const float wishLen = glm::length(wish);
  if (wishLen > 1e-4f)
  {
    wish /= wishLen;
  }
  else
  {
    wish = glm::vec3(0.0f);
    speed = 0.0f;
  }

  glm::vec3 eye = GetLocomotionEye();
  CreatureInput verticalInput;
  verticalInput.jumpHeld = Intent.wantJump;
  ApplyCreatureMotorStep(world, eye, Locomotion, verticalInput, wish, speed, dt,
                         Id, world.IsStepUpEnabled());
  SyncFeetFromLocomotion(world, eye);
  SyncBoundsFromStance();
  // Corner / lip traps: motor axis resolve can leave AABB intersecting solids.
  {
    const glm::vec3 size = Bounds.profile.restSizeBlocks;
    if (world.DepenetrateBlockBodyXZ(BodyOrigin, size))
    {
      eye = GetLocomotionEye();
      // Keep feet anchored after XZ nudge so next-frame step-up stays eligible.
      Locomotion.SyncFeetAnchorFromView(BodyOrigin.y, true);
    }
  }

  if (glm::length(wish) > 1e-4f)
  {
    const glm::vec3 size = Bounds.profile.restSizeBlocks;
    const float maxClimbDrop =
        Locomotion.GetCapabilities().jumpHeightBlocks;
    if (!HabitatAllowsMovementAt(world, habitat, BodyOrigin, size,
                                 maxClimbDrop))
    {
      const glm::vec3 attempted = BodyOrigin;
      const glm::vec2 xzTravel(attempted.x - bodyOriginBefore.x,
                              attempted.z - bodyOriginBefore.z);
      bool accepted = false;
      if (habitat == CreatureHabitat::Terrestrial &&
          glm::dot(xzTravel, xzTravel) > 1e-10f)
      {
        // Keep XZ travel; re-snap feet within jumpHeight instead of full revert.
        const glm::vec3 recovered = ResolveTerrestrialMobMovement(
            world, bodyOriginBefore,
            glm::vec3(xzTravel.x, 0.0f, xzTravel.y), size, Id, maxClimbDrop,
            maxClimbDrop);
        if (HabitatAllowsMovementAt(world, habitat, recovered, size,
                                    maxClimbDrop) &&
            glm::dot(glm::vec2(recovered.x - bodyOriginBefore.x,
                               recovered.z - bodyOriginBefore.z),
                     glm::vec2(recovered.x - bodyOriginBefore.x,
                               recovered.z - bodyOriginBefore.z)) > 1e-10f)
        {
          BodyOrigin = recovered;
          accepted = true;
        }
        else
        {
          glm::vec3 kept(attempted.x, bodyOriginBefore.y, attempted.z);
          if (HabitatAllowsMovementAt(world, habitat, kept, size, maxClimbDrop))
          {
            BodyOrigin = kept;
            accepted = true;
          }
        }
      }
      if (!accepted)
      {
        if (UCreatureMovementDiagnostics::IsEnabled())
        {
          CreatureMovementDiagRecord rec;
          rec.event = "habitat_reject";
          rec.creatureId = Id;
          rec.typeId = TypeId;
          rec.habitat = ToString(habitat);
          rec.body = attempted;
          rec.intentDir = diagIntentDir;
          rec.intentSpeed = diagIntentSpeed;
          rec.travel = attempted - bodyOriginBefore;
          rec.reason = "habitat_allows_false";
          UCreatureMovementDiagnostics::Record(rec);
        }
        BodyOrigin = bodyOriginBefore;
      }
      eye = GetLocomotionEye();
      Locomotion.SyncFeetAnchorFromView(BodyOrigin.y, true);
      SyncBoundsFromStance();
    }
  }

  if (!Possessed)
  {
    const CollisionVolume vol = GetCollisionVolume();
    const SampledFluidState fluid = world.SampleFluidPhysicsVolume(vol);
    if ((habitat == CreatureHabitat::Aquatic ||
         habitat == CreatureHabitat::Amphibious ||
         habitat == CreatureHabitat::Lava) &&
        fluid.inFluid && glm::length(wish) < 1e-4f)
    {
      const float sink = fluid.SinkSpeed * dt * 0.2f;
      glm::vec3 sinkEye = GetLocomotionEye();
      ApplyCreatureMotorStep(world, sinkEye, Locomotion, verticalInput,
                             glm::vec3(0.0f, -1.0f, 0.0f), sink / dt, dt, Id,
                             false);
      SyncFeetFromLocomotion(world, sinkEye);
    }
  }

  if (!Possessed)
  {
    // Face travel only — intent yaw with zero travel looks like spinning in place.
    const glm::vec3 xzDelta = BodyOrigin - bodyOriginBefore;
    const glm::vec2 faceDir(xzDelta.x, xzDelta.z);
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
  if (UCreatureMovementDiagnostics::IsEnabled() &&
      glm::length(diagIntentDir) > 1e-4f)
  {
    CreatureMovementDiagRecord rec;
    rec.event =
        glm::length(rawInput.bodyOriginAfter - rawInput.bodyOriginBefore) < 1e-5f
            ? "blocked"
            : "intent";
    rec.creatureId = Id;
    rec.typeId = TypeId;
    rec.body = BodyOrigin;
    rec.intentDir = diagIntentDir;
    rec.intentSpeed = diagIntentSpeed;
    rec.travel = BodyOrigin - bodyOriginBefore;
    rec.onGround = LocomotionFacts.onGround;
    rec.inFluid = LocomotionFacts.inFluid;
    rec.reason = rec.event == "blocked" ? "zero_travel" : "exec_frame";
    UCreatureMovementDiagnostics::Record(rec);
  }
  LastBodyOrigin = BodyOrigin;
}

void UCreature::UpdateControlled(UWorld &world, const CreatureInput &input,
                                 float dt)
{
  ClearIntent();
  CreatureInput gated = input;
  gated.allowSprint = Vitals.fatigue < Vitals.maxFatigue * 0.95f;
  const CreatureLocomotionCapabilities baseCaps = Locomotion.GetCapabilities();
  CreatureLocomotionCapabilities caps = baseCaps;
  const float agi =
      0.75f + static_cast<float>(Attributes.agility) / 40.f;
  const float fatiguePenalty =
      Vitals.maxFatigue > 0.f
          ? (1.f - 0.4f * (Vitals.fatigue / Vitals.maxFatigue))
          : 1.f;
  caps.walkSpeed *= agi * std::max(0.4f, fatiguePenalty);
  Locomotion.SetCapabilities(caps);
  glm::vec3 eye = GetLocomotionEye();
  Locomotion.UpdateLocomotion(&world, eye, gated, dt, Id);
  Locomotion.SetCapabilities(baseCaps);
  SyncFeetFromLocomotion(world, eye);
  SyncBoundsFromStance();
}

} // namespace cutum
