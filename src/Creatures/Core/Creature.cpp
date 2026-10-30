#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureBounds.h"
#include "Creatures/Core/CreatureFacing.h"
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

namespace
{

/// Highest ground under the body footprint (center, edges and corners) at or
/// below referenceFeetY + 0.25. Matches HasBodySupport: a body wider than a
/// hole rests on its rim, so the center column alone snapped it down into
/// the hole and left it embedded in the rim.
std::optional<float> QueryFootprintGroundFeetY(const UWorld &world,
                                               const glm::vec3 &bodyOrigin,
                                               const glm::vec3 &sizeBlocks,
                                               float referenceFeetY)
{
  const float hx = std::max(0.0f, sizeBlocks.x * 0.5f - 0.05f);
  const float hz = std::max(0.0f, sizeBlocks.z * 0.5f - 0.05f);
  std::optional<float> best;
  for (const float dx : {0.0f, -hx, hx})
  {
    for (const float dz : {0.0f, -hz, hz})
    {
      if (const std::optional<float> groundY = world.QueryGroundFeetYUnder(
              WorldCoordToBlockIndex(bodyOrigin.x + dx),
              WorldCoordToBlockIndex(bodyOrigin.z + dz), referenceFeetY))
      {
        if (!best || *groundY > *best)
        {
          best = groundY;
        }
      }
    }
  }
  return best;
}

} // namespace

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

void UCreature::SetPlayerCharacter(bool v) { PlayerCharacter = v; }

void UCreature::SetPossessed(bool v) { Possessed = v; }


void UCreature::ApplyStatsFromDefinition(const CreatureDefinition &def)
{
  Vitals = def.stats.vitalsTemplate;
  Attributes = def.stats.attributes;
  NeedsTick = def.stats.needsTick;
  Armor = def.stats.armorGroups;
  BareHandFleshyOverride =
      def.stats.bareHand.hasOverride ? def.stats.bareHand.fleshyDamage : -1;
  BareHandIntervalOverride = def.stats.bareHand.hasOverride
                                 ? def.stats.bareHand.fullPunchInterval
                                 : -1.f;
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
  return CollisionVolumeFromBody(BodyOrigin, GetCollisionSize());
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
    const float maxSnap =
        std::max(0.25f, Locomotion.GetCapabilities().jumpHeightBlocks);
    if (const std::optional<float> groundY = QueryFootprintGroundFeetY(
            world, BodyOrigin, Bounds.profile.restSizeBlocks, refFeetY))
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
  const LocomotionState previousState = LocomotionFacts.state;
  LocomotionFacts = raw;
  CreatureLocomotionRawInput deriveInput = input;
  deriveInput.previousState = previousState;
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
  if (TryIdleIntentFastPath(world, dt, bodyOriginBefore, habitat, airMobility))
  {
    return;
  }

  UpdateMovementModeForHabitat(habitat, airMobility);

  glm::vec3 wish = Intent.moveDirWorld;
  float speed = Intent.moveSpeed;
  ShapeMovementWishAndSpeed(world, habitat, airMobility, wish, speed);
  CreatureInput verticalInput;
  verticalInput.jumpHeld = Intent.wantJump;

  glm::vec3 eye = GetLocomotionEye();
  const UCreatureLocomotionController locomotionBefore = Locomotion;
  RunMotorStep(world, eye, verticalInput, wish, speed, dt);

  EnforceHabitatAfterMove(world, habitat, bodyOriginBefore, locomotionBefore,
                          diagIntentDir, diagIntentSpeed, wish);

  ApplyIdleFluidSink(world, habitat, wish, dt, verticalInput);

  FaceTravelDirection(bodyOriginBefore, wish);
  UpdateFootprintOrientation(world);

  if (Intent.clearOnApply)
  {
    ClearIntent();
  }

  FinishIntentFrame(world, dt, bodyOriginBefore, diagIntentDir,
                    diagIntentSpeed);
}

bool UCreature::TryIdleIntentFastPath(UWorld &world, float dt,
                                      const glm::vec3 &bodyOriginBefore,
                                      CreatureHabitat habitat,
                                      bool airMobility)
{
  const bool idle_intent = glm::length(Intent.moveDirWorld) < 1e-4f &&
                           Intent.moveSpeed < 1e-4f && !Intent.wantJump;
  if (!idle_intent || !Locomotion.IsOnGround() || airMobility ||
      habitat == CreatureHabitat::Aquatic ||
      habitat == CreatureHabitat::Lava || LocomotionFacts.inFluid)
  {
    return false;
  }
  // Revalidate the support the controller last saw: blocks under an idle
  // body can be removed by the player, and the fast path skips the motor,
  // so gravity would never re-engage (runtime issue 1). Falls through to
  // the full motor (gravity) when the ground is gone.
  {
    const CollisionVolume supportVol =
        CollisionVolumeFromBody(BodyOrigin, Bounds.profile.restSizeBlocks);
    if (!world.HasBodySupport(supportVol))
    {
      Locomotion.BreakGroundSupport();
      return false;
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
  return true;
}

void UCreature::UpdateMovementModeForHabitat(CreatureHabitat habitat,
                                             bool airMobility)
{
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
}

void UCreature::ShapeMovementWishAndSpeed(const UWorld &world,
                                          CreatureHabitat habitat,
                                          bool airMobility, glm::vec3 &wish,
                                          float &speed)
{
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
        ProbeEnvironmentAtMemoized(world, BodyOrigin,
                                   Bounds.profile.restSizeBlocks);
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
}

void UCreature::RunMotorStep(UWorld &world, glm::vec3 &eye,
                             const CreatureInput &verticalInput,
                             const glm::vec3 &wish, float speed, float dt)
{
  ApplyCreatureMotorStep(world, eye, Locomotion, verticalInput, wish, speed,
                         dt, Id, world.IsStepUpEnabled());
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
}

void UCreature::EnforceHabitatAfterMove(
    UWorld &world, CreatureHabitat habitat, const glm::vec3 &bodyOriginBefore,
    const UCreatureLocomotionController &locomotionBefore,
    const glm::vec3 &diagIntentDir, float diagIntentSpeed,
    const glm::vec3 &wish)
{
  if (glm::length(wish) <= 1e-4f)
  {
    return;
  }
  const glm::vec3 size = Bounds.profile.restSizeBlocks;
  const float maxClimbDrop = Locomotion.GetCapabilities().jumpHeightBlocks;
  if (HabitatAllowsMovementAt(world, habitat, BodyOrigin, size, maxClimbDrop))
  {
    return;
  }

  // A creature out of its element may still DESCEND: a mob falling from a
  // high spawn (or an aquatic hovering above the sea) used to have every
  // move - including the fall - reverted AND its feet re-anchored at the
  // stuck position, pinning it mid-air forever. Accept strictly descending
  // post-motor positions (gravity always carries a creature toward valid
  // habitat); level and ascending moves stay gated.
  // Flyers do not fall, so they get no descent pass: a wasp gliding down
  // into water was accepted here and then pinned on the bottom, every move
  // out being "level or ascending".
  if (habitat != CreatureHabitat::Aerial &&
      BodyOrigin.y < bodyOriginBefore.y - 1.0e-4f)
  {
    Locomotion.SyncFeetAnchorFromView(BodyOrigin.y, true);
    SyncBoundsFromStance();
    return;
  }
  // Already out of its element before this move: a move cannot make that
  // worse, and rejecting it froze the creature where it was trapped.
  if (!HabitatAllowsMovementAt(world, habitat, bodyOriginBefore, size,
                               maxClimbDrop))
  {
    Locomotion.SyncFeetAnchorFromView(BodyOrigin.y, true);
    SyncBoundsFromStance();
    return;
  }

  const glm::vec3 attempted = BodyOrigin;
  const glm::vec2 xzTravel(attempted.x - bodyOriginBefore.x,
                           attempted.z - bodyOriginBefore.z);
  bool accepted = false;
  if (habitat == CreatureHabitat::Terrestrial &&
      glm::dot(xzTravel, xzTravel) > 1e-10f)
  {
    // Keep XZ travel; re-snap feet within jumpHeight instead of full revert.
    const glm::vec3 recovered = ResolveTerrestrialMobMovement(
        world, bodyOriginBefore, glm::vec3(xzTravel.x, 0.0f, xzTravel.y), size,
        Id, maxClimbDrop, maxClimbDrop);
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
    // The move did not happen, so neither did the motor's view of it: the
    // body stayed put while the controller kept "falling" off the rejected
    // spot, and gravity grew to -50 over seconds of standing still.
    Locomotion = locomotionBefore;
  }
  Locomotion.SyncFeetAnchorFromView(BodyOrigin.y, true);
  SyncBoundsFromStance();
}

void UCreature::ApplyIdleFluidSink(UWorld &world, CreatureHabitat habitat,
                                   const glm::vec3 &wish, float dt,
                                   const CreatureInput &verticalInput)
{
  if (Possessed)
  {
    return;
  }
  const CollisionVolume vol = GetCollisionVolume();
  const SampledFluidState fluid = world.SampleFluidPhysicsVolume(vol);
  if ((habitat == CreatureHabitat::Aquatic ||
       habitat == CreatureHabitat::Amphibious ||
       habitat == CreatureHabitat::Lava) &&
      fluid.inFluid && glm::length(wish) < 1e-4f && dt > 1e-6f)
  {
    const float sink = fluid.SinkSpeed * dt * 0.2f;
    glm::vec3 sinkEye = GetLocomotionEye();
    ApplyCreatureMotorStep(world, sinkEye, Locomotion, verticalInput,
                           glm::vec3(0.0f, -1.0f, 0.0f), sink / dt, dt, Id,
                           false);
    SyncFeetFromLocomotion(world, sinkEye);
  }
}

void UCreature::FaceTravelDirection(const glm::vec3 &bodyOriginBefore,
                                    const glm::vec3 &wish)
{
  if (Possessed)
  {
    return;
  }
  // Face travel only — intent yaw with zero travel looks like spinning in
  // place — and only travel that agrees with the wish (not pushes).
  const glm::vec3 xzDelta = BodyOrigin - bodyOriginBefore;
  if (const std::optional<glm::vec2> face = FacingDirectionFromMove(
          glm::vec2(xzDelta.x, xzDelta.z), glm::vec2(wish.x, wish.z)))
  {
    const glm::vec2 faceDir = *face;
    // Pure facing: the visual adds the model yaw offset when drawing (adding
    // it here too turned any species with a non-zero offset twice).
    Yaw = ModelYawFromDirection(faceDir.x, faceDir.y);
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

void UCreature::UpdateFootprintOrientation(const UWorld &world)
{
  if (PlayerCharacter || Possessed)
  {
    return;
  }
  const glm::vec3 rest = Bounds.profile.restSizeBlocks;
  if (std::abs(rest.x - rest.z) < 0.05f ||
      WantsSwappedFootprint(Yaw, FootprintSwapped) == FootprintSwapped)
  {
    return;
  }
  const glm::vec3 turned = SwapFootprintXZ(rest);
  const CollisionVolume vol = CollisionVolumeFromBody(BodyOrigin, turned);
  if (world.CheckBlockCollisionVolume(vol) ||
      world.CheckCreatureCollisionVolume(vol, Id))
  {
    // No room to turn the box here (corridor, against a wall, a neighbour
    // alongside): keep the old orientation and try again on a later frame.
    // Turning into a neighbour got the body pushed back, it then faced the
    // push and turned back: a per-frame swap/push loop (Run/Walk flapping).
    return;
  }
  Bounds.profile.restSizeBlocks = turned;
  Bounds.profile.minSizeBlocks = SwapFootprintXZ(Bounds.profile.minSizeBlocks);
  Bounds.profile.maxSizeBlocks = SwapFootprintXZ(Bounds.profile.maxSizeBlocks);
  Bounds.currentSizeBlocks = SwapFootprintXZ(Bounds.currentSizeBlocks);
  Locomotion.SetCollisionProfile(Bounds.profile.restSizeBlocks, EyeOffset.y);
  FootprintSwapped = !FootprintSwapped;
}

void UCreature::FinishIntentFrame(UWorld &world, float dt,
                                  const glm::vec3 &bodyOriginBefore,
                                  const glm::vec3 &diagIntentDir,
                                  float diagIntentSpeed)
{
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

} // namespace cutum
