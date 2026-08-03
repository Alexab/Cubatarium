#include "World/Core/WorldViewBinding.h"

#include "Activity/WorldCreatureActivitySink.h"
#include "Creatures/Combat/CreatureCombat.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureBounds.h"
#include "Creatures/Influence/InfluenceApplier.h"
#include "Creatures/Influence/InfluenceResolver.h"
#include "Creatures/Influence/StatusEffectSystem.h"
#include "Creatures/Locomotion/CreatureLocomotionController.h"
#include "Creatures/Locomotion/LocomotionTypes.h"
#include "Creatures/Player/PlayerCapsule.h"
#include "Creatures/Player/User.h"
#include "Creatures/Stats/CreatureVitalsSystem.h"
#include "Creatures/Visual/CreaturePartMeshData.h"
#include "Render/Camera/Camera.h"
#include "Render/Engine/ViewEngine.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Chunks/TerrainColumnUtil.h"
#include "World/Core/World.h"
#include "World/Diagnostics/MovementDiagnosticsRecorder.h"
#include "World/Math/GridMath.h"
#include "World/Mesh/WorldMeshService.h"
#include "World/Streaming/WorldStreaming.h"

#include <chrono>
#include <optional>

namespace cutum
{

namespace
{

constexpr float kMinReasonablePlayerY = -32.0f;

} // namespace

std::string
UWorldViewBinding::ResolveUserName(const UWorld &world,
                                   const std::shared_ptr<UUser> &user)
{
  for (const auto &entry : world.Users)
  {
    if (entry.second == user)
    {
      return entry.first;
    }
  }
  return world.CurrentUserName;
}

UWorldViewBinding::UWorldViewBinding(std::shared_ptr<UViewEngine> engine)
    : Engine_(std::move(engine))
{
}

std::shared_ptr<UCamera>
UWorldViewBinding::GetUserCamera(const UWorld &world,
                                 const std::string &userName) const
{
  if (!Engine_)
  {
    return nullptr;
  }
  const auto user = const_cast<UWorld &>(world).GetUser(userName);
  if (!user)
  {
    return nullptr;
  }
  return Engine_->GetCamera(user->GetViewId());
}

std::shared_ptr<UCamera>
UWorldViewBinding::GetCurrentUserCamera(UWorld &world) const
{
  return GetUserCamera(world, world.CurrentUserName);
}

std::shared_ptr<UCamera>
UWorldViewBinding::GetCurrentUserCamera(const UWorld &world) const
{
  return GetUserCamera(world, world.CurrentUserName);
}

void UWorldViewBinding::ApplySpawnToCamera(UWorld &world)
{
  glm::vec3 spawn = world.SpawnPoint;
  const auto camera = GetCurrentUserCamera(world);
  const PlayerCapsule cap =
      camera ? camera->GetPlayerCapsule() : PlayerCapsule::Standing();
  world.DepenetrateEye(spawn, cap, world.GetMovementCollisionSkipId());
  world.SpawnPoint = spawn;

  if (auto user = world.GetCurrentUser())
  {
    user->SetPosition(world.SpawnPoint);
    user->SetCameraOrientation(-90.0f, 0.0f);
  }
  if (camera)
  {
    camera->SetPosition(world.SpawnPoint);
    camera->SetOrientation(-90.0f, 0.0f);
    camera->ApplyWorldViewSettings(world.GetViewSettings());
    if (const UCreature *controlled = world.GetControlledCreature())
    {
      if (const CreatureDefinition *def =
              world.GetCreatureDefinition(controlled->GetTypeId()))
      {
        world.ApplyLocomotionDefinitionToCamera(*camera, *def);
      }
    }
    return;
  }
  if (Engine_ && Engine_->GetActiveCamera())
  {
    Engine_->GetActiveCamera()->SetPosition(world.SpawnPoint);
    Engine_->GetActiveCamera()->SetOrientation(-90.0f, 0.0f);
    Engine_->GetActiveCamera()->ApplyWorldViewSettings(world.GetViewSettings());
  }
}

void UWorldViewBinding::ApplyUserToCamera(UWorld &world,
                                          const std::shared_ptr<UUser> &user)
{
  if (!user)
  {
    return;
  }
  world.SanitizeUserPosition(user);
  const std::string userName = ResolveUserName(world, user);
  if (auto camera = GetUserCamera(world, userName))
  {
    camera->SetPosition(user->GetPosition());
    camera->SetOrientation(user->GetCameraYaw(), user->GetCameraPitch());
    camera->ApplyWorldViewSettings(world.GetViewSettings());
    if (const UCreature *controlled = world.GetControlledCreature())
    {
      if (const CreatureDefinition *def =
              world.GetCreatureDefinition(controlled->GetTypeId()))
      {
        world.ApplyLocomotionDefinitionToCamera(*camera, *def);
      }
    }
  }
}

void UWorldViewBinding::WarmupVisibleListAtCamera(UWorld &world)
{
  const auto camera = GetCurrentUserCamera(world);
  if (!camera)
  {
    return;
  }
  const glm::mat4 view = camera->GetViewMatrix();
  const glm::mat4 proj = camera->GetProjection();
  const glm::mat4 vp = proj * view;
  world.MeshService->WarmupVisibleListFromViewProj(vp, camera->GetPosition());
}

void UWorldViewBinding::EnsurePlayerOnGround(UWorld &world)
{
  auto user = world.GetCurrentUser();
  if (!user || !world.BlockRegistry)
  {
    return;
  }

  const std::string userName = ResolveUserName(world, user);
  auto camera = GetUserCamera(world, userName);
  if (!camera)
  {
    return;
  }

  const PlayerCapsule cap =
      camera ? camera->GetPlayerCapsule() : PlayerCapsule::Standing();
  glm::vec3 pos = user->GetPosition();
  const glm::ivec3 column = WorldPosToBlock(pos);
  int x = column.x;
  int z = column.z;

  std::optional<int> topY = world.FindHighestSolidY(x, z);
  if (!topY)
  {
    const glm::ivec3 spawnColumn = WorldPosToBlock(world.SpawnPoint);
    x = spawnColumn.x;
    z = spawnColumn.z;
    topY = world.FindHighestSolidY(x, z);
  }
  if (!topY)
  {
    ApplySpawnToCamera(world);
    if (auto cam = GetUserCamera(world, userName))
    {
      cam->ResetVerticalPhysics();
    }
    return;
  }

  pos = BlockCenter(glm::ivec3(x, *topY, z));
  pos.y = BlockTopY(*topY) + cap.eyeHeight;
  world.DepenetrateEye(pos, cap, world.GetMovementCollisionSkipId());

  user->SetPosition(pos);
  camera->SetPosition(pos);
  camera->ResetVerticalPhysics();
}

void UWorldViewBinding::ResetCurrentCameraVerticalPhysics(UWorld &world)
{
  if (auto camera = GetCurrentUserCamera(world))
  {
    camera->ResetVerticalPhysics();
  }
}

size_t UWorldViewBinding::CreateUserCamera(const glm::vec3 &spawnEye)
{
  if (!Engine_)
  {
    return 0;
  }
  auto camera = std::make_shared<UCamera>(spawnEye, glm::vec3(0.0f, 1.0f, 0.0f),
                                          -90.0f, 0.0f);
  camera->SetFreeMove(false);
  return Engine_->AddCameraReturnId(camera);
}

void UWorldViewBinding::SetActiveCamera(size_t viewId)
{
  if (Engine_)
  {
    Engine_->SetActiveCamera(viewId);
  }
}

PlayerCapsule
UWorldViewBinding::ResolvePlacementCapsule(const UWorld &world) const
{
  if (const auto camera = GetCurrentUserCamera(world))
  {
    return camera->GetPlayerCapsule();
  }
  return PlayerCapsule::Standing();
}

bool UWorldViewBinding::TryGetCurrentViewRay(const UWorld &world,
                                             glm::vec3 &position,
                                             glm::vec3 &front) const
{
  const auto camera = GetCurrentUserCamera(world);
  if (!camera)
  {
    return false;
  }
  return camera->TryGetCenterViewRay(position, front);
}

void UWorldViewBinding::RefreshIntersectionFromCurrentView(UWorld &world)
{
  glm::vec3 position;
  glm::vec3 front;
  if (TryGetCurrentViewRay(world, position, front))
  {
    world.UpdateIntersection(position, front);
  }
}

std::shared_ptr<UViewEngine> UWorld::GetViewEngine() const
{
  return ViewBinding ? ViewBinding->GetEngine() : nullptr;
}

std::shared_ptr<UCamera> UWorld::GetUserCamera(const std::string &Name)
{
  return ViewBinding ? ViewBinding->GetUserCamera(*this, Name) : nullptr;
}

std::shared_ptr<UCamera> UWorld::GetCurrentUserCamera()
{
  return ViewBinding ? ViewBinding->GetCurrentUserCamera(*this) : nullptr;
}

std::shared_ptr<UCamera> UWorld::GetCurrentUserCamera() const
{
  return ViewBinding ? ViewBinding->GetCurrentUserCamera(*this) : nullptr;
}

void UWorld::ApplySpawnToCamera()
{
  if (ViewBinding)
  {
    ViewBinding->ApplySpawnToCamera(*this);
  }
}

void UWorld::ApplyUserToCamera(const std::shared_ptr<UUser> &user)
{
  if (ViewBinding)
  {
    ViewBinding->ApplyUserToCamera(*this, user);
  }
}

void UWorld::WarmupVisibleListAtCamera()
{
  if (ViewBinding)
  {
    ViewBinding->WarmupVisibleListAtCamera(*this);
  }
}

void UWorld::EnsurePlayerOnGround()
{
  if (ViewBinding)
  {
    ViewBinding->EnsurePlayerOnGround(*this);
  }
}

bool UWorld::AddObjectByView()
{
  glm::vec3 position;
  glm::vec3 front;
  if (!ViewBinding ||
      !ViewBinding->TryGetCurrentViewRay(*this, position, front))
  {
    return false;
  }
  return AddObjectByView(position, front);
}

bool UWorld::DelObjectByView()
{
  glm::vec3 position;
  glm::vec3 front;
  if (!ViewBinding ||
      !ViewBinding->TryGetCurrentViewRay(*this, position, front))
  {
    return false;
  }
  return DelObjectByView(position, front);
}

bool UWorld::PlaceActiveObjectByView()
{
  glm::vec3 position;
  glm::vec3 front;
  if (!ViewBinding ||
      !ViewBinding->TryGetCurrentViewRay(*this, position, front))
  {
    return false;
  }
  return PlaceActiveObjectByView(position, front);
}

void UWorld::UpdateIntersection(const glm::vec3 &position,
                                const glm::vec3 &front)
{
  IsIntersectionExists = CheckRayIntersection(
      position, front, Intersection, IntersectionDistance,
      IntersectionCubeIndex, IntersectionCubeSide, IntersectionObjectIndex);

  const PlayerCapsule cap = ViewBinding
                                ? ViewBinding->ResolvePlacementCapsule(*this)
                                : PlayerCapsule::Standing();
  float max_distance = 8.0f;
  glm::vec3 player_eye = position;
  if (auto camera = GetCurrentUserCamera())
  {
    max_distance = camera->GetBlockInteractMaxDistance();
    player_eye = camera->GetPosition();
  }
  const BlockPlacementResolve resolved = Collision.ResolveBlockPlacement(
      position, front, cap, max_distance, player_eye);

  HasIntersectionBlock = resolved.break_hit.has_value();
  PlaceTargetActive = resolved.place_block_pos.has_value();
  if (resolved.break_hit)
  {
    IntersectionBlockPos = resolved.break_hit->blockPos;
  }
  else
  {
    IntersectionBlockPos = glm::ivec3(0);
  }
  PlaceBlockPos = resolved.place_block_pos.value_or(glm::ivec3(0));

  if (auto user = GetCurrentUser())
  {
    if (auto camera = GetCurrentUserCamera())
    {
      user->SetPosition(camera->GetPosition());
      user->SetCameraOrientation(camera->GetYaw(), camera->GetPitch());
    }
  }
}

void UWorld::RunLegacyPhysicsFrame()
{
  if (!BlockWorldReady)
  {
    return;
  }
  if (PhysicsSuspendFrames > 0)
  {
    --PhysicsSuspendFrames;
    return;
  }

  auto t_begin = std::chrono::high_resolution_clock::now();
  Streaming->ResetFrameTiming();

  auto camera = GetCurrentUserCamera();
  UCreature *controlled = GetControlledCreature();
  if (camera && camera->GetPosition().y < kMinReasonablePlayerY)
  {
    EnsurePlayerOnGround();
    camera->ResetVerticalPhysics();
    return;
  }
  const float prevPlayerY = camera ? camera->GetPosition().y : 0.0f;
  const float dt = camera ? camera->GetDeltaTime() : 0.0f;
  glm::ivec3 feetBlockForReadiness(0);
  bool hasFeetBlockForReadiness = false;
  static bool prev_collision_ring_ready = true;

  if (camera && Streaming->HasStreamer() && IsStreamingEnabled())
  {
    const glm::vec3 eyePos = camera->GetPosition();
    float feetY =
        FeetYFromEye(eyePos, controlled ? controlled->GetEyeOffset().y
                                         : PlayerCapsule::Standing().eyeHeight);
    if (controlled)
    {
      feetY = BoundsFeetY(controlled->GetBodyOrigin());
    }
    const glm::ivec3 feetBlock =
        WorldPosToBlock(glm::vec3(eyePos.x, feetY + 0.01f, eyePos.z));
    feetBlockForReadiness = feetBlock;
    hasFeetBlockForReadiness = true;
    glm::vec3 forward = camera->GetFront();
    forward.y = 0.0f;
    if (prev_collision_ring_ready || (PhysicsTickCounter % 2) == 0)
    {
      Streaming->EnsureCollisionChunks(feetBlock, forward);
    }
  }

  UWorldCreatureActivitySink activitySink(*this);
  Environment.TickActivity(*this, activitySink, dt);

  ForEachCreature(
      [&](UCreature &creature)
      {
        creature.AdvanceInfluenceCooldown(dt);
        creature.TickHitFlash(dt);
        if (Environment.GetControlledCreatureId() != 0 &&
            creature.GetId() == Environment.GetControlledCreatureId())
        {
          return;
        }
        if (creature.IsPossessed())
        {
          return;
        }
        const CreatureIntent &intent = creature.GetIntent();
        if (intent.Influence.Channel == InfluenceChannel::Melee &&
            intent.Influence.TargetId != 0)
        {
          CreatureCombat::TryMeleeStrike(*this, creature, GetGameMode());
        }
        creature.ExecuteIntent(*this, dt);
      });

  // Controlled / possessed player: resolve Influence after agents (input may
  // have set Melee / Dig via PlayerInteractionRouter). Dig Intent is not
  // player-gated in Resolver; session tick uses the world DigSession.
  if (Environment.GetControlledCreatureId() != 0)
  {
    if (UCreature *controlled =
            GetCreature(Environment.GetControlledCreatureId()))
    {
      const CreatureIntent &intent = controlled->GetIntent();
      if (intent.Influence.Channel == InfluenceChannel::Dig)
      {
        InfluencePrediction pred =
            InfluenceResolver::Resolve(*this, *controlled, GetGameMode(),
                                       nullptr);
        InfluenceApplier::Apply(*this, pred, GetGameMode(), dt);
        if (!HasBreakSession())
        {
          CreatureIntent cleared = controlled->GetIntent();
          cleared.Influence = InfluenceIntent{};
          controlled->SetIntent(cleared);
        }
      }
      else if (intent.Influence.Channel == InfluenceChannel::Melee &&
               intent.Influence.TargetId != 0)
      {
        CreatureCombat::TryMeleeStrike(*this, *controlled, GetGameMode());
        // One-shot: clear Melee Intent after resolve attempt so hold-LMB dig
        // does not spam; cooldown still gates actual hits.
        CreatureIntent cleared = intent;
        cleared.attackTargetId = 0;
        cleared.Influence = InfluenceIntent{};
        controlled->SetIntent(cleared);
      }
    }
  }

  bool is_moved = camera && camera->DoMovement(this);
  static bool was_collision_ready = true;
  const bool collision_ready =
      !hasFeetBlockForReadiness ||
      (camera && camera->GetFreeMove()) ||
      IsCollisionReadyAtFeet(feetBlockForReadiness);
  if (!collision_ready && camera)
  {
    PhysicsTelemetryData.CollisionReadyWaitMs +=
        static_cast<double>(camera->GetDeltaTime()) * 1000.0;
  }
  if (collision_ready != was_collision_ready)
  {
    ++PhysicsTelemetryData.CollisionReadyTransitions;
    was_collision_ready = collision_ready;
  }
  prev_collision_ring_ready = collision_ready;
  if (camera && hasFeetBlockForReadiness && !collision_ready)
  {
    camera->SuspendFallThroughUnloadedChunks();
  }
  if (Streaming && Streaming->GetStreamer() && hasFeetBlockForReadiness)
  {
    const glm::ivec3 feet_chunk =
        UChunkManager::WorldToChunk(feetBlockForReadiness);
    Streaming->GetStreamer()->SetCollisionUrgentRing(
        feet_chunk, PhysicsBudgetConfig.CollisionSafetyRadiusChunks,
        !collision_ready);
  }

  if (controlled && camera)
  {
    const glm::vec3 eye = camera->GetPosition();
    float feetY = FeetYFromEye(eye, controlled->GetEyeOffset().y);
    if (!camera->GetFreeMove() && camera->HasAnchoredFeet())
    {
      const int gx = WorldCoordToBlockIndex(eye.x);
      const int gz = WorldCoordToBlockIndex(eye.z);
      if (const std::optional<float> gy = QueryGroundFeetYUnder(gx, gz, feetY))
      {
        feetY = *gy;
      }
    }
    controlled->SetBodyOrigin(glm::vec3(eye.x, feetY, eye.z));
    controlled->GetLocomotion().SetStanceBlendForView(camera->GetStanceBlend());
    controlled->GetLocomotion().SyncFeetAnchorFromView(
        feetY, camera->HasAnchoredFeet());
    const CreatureViewOrientation view_orient =
        camera->ResolveCreatureViewOrientation();
    controlled->SetOrientation(view_orient.YawDeg, view_orient.PitchDeg);
    controlled->SyncBoundsFromStance();
    controlled->GetLocomotion().SetMode(camera->GetFreeMove()
                                            ? CreatureMovementMode::Flying
                                            : CreatureMovementMode::Walking);
    float horizontalSpeed = 0.0f;
    const UCreatureLocomotionController &camLoc =
        camera->GetLocomotionController();
    const LocomotionState camState = camLoc.GetLocomotionState();
    const PlayerInput moveInput = camera->GetMovementInput();
    if (camState == LocomotionState::Walk || camState == LocomotionState::Run ||
        camState == LocomotionState::Crouch)
    {
      horizontalSpeed = camLoc.ResolveHorizontalSpeed(moveInput);
    }
    else if (camState == LocomotionState::Fly ||
             camState == LocomotionState::Glide ||
             camState == LocomotionState::Hover)
    {
      horizontalSpeed = camLoc.ResolveHorizontalSpeed(moveInput);
    }
    // P3: soft streaming integrity clamp (same scale as Camera::ProcessKeyboard).
    {
      const float clamp = GetPhysicsTelemetry().StreamSpeedClampScale;
      if (clamp > 0.0f && clamp < 1.0f)
      {
        horizontalSpeed *= clamp;
      }
    }
    controlled->RebuildLocomotionFactsFromController(
        camLoc, controlled->GetLocomotion().GetCapabilities(),
        static_cast<float>(camera->GetDeltaTime()), horizontalSpeed, this);
    is_moved = true;
  }

  // After player facts sync so sprint/swim fatigue sees this frame's state.
  CreatureVitalsSystem::Tick(*this, GetGameMode(), GetDifficulty(), dt);
  StatusEffectSystem::Tick(*this, GetGameMode(), dt);

  const auto t_after_move = std::chrono::high_resolution_clock::now();
  PhysicsTelemetryData.StreamMs = 0.0;
  PhysicsTelemetryData.StreamerUpdateMs = 0.0;
  PhysicsTelemetryData.AsyncIoMs = 0.0;
  PhysicsTelemetryData.RelightDrainMs = 0.0;
  PhysicsTelemetryData.MeshEmergeMs = 0.0;
  PhysicsTelemetryData.MeshEmergePrepMs = 0.0;

  if (camera)
  {
    const auto t_before_stream = std::chrono::high_resolution_clock::now();
    GetMeshService().BeginHoleQueryFrame();
    UpdateStreaming();
    TickAsyncChunkSystems();
    const auto t_after_stream = std::chrono::high_resolution_clock::now();
    TickEnterGameMeshBurst();
    TickMeshEmerge();
    const auto t_after_mesh = std::chrono::high_resolution_clock::now();
    BlockWorldReady = true;

    PhysicsTelemetryData.StreamMs =
        std::chrono::duration<double, std::milli>(t_after_stream - t_before_stream)
            .count();
    PhysicsTelemetryData.MeshEmergeMs =
        std::chrono::duration<double, std::milli>(t_after_mesh - t_after_stream)
            .count();
    PhysicsTelemetryData.MeshSyncMs =
        GetMeshService().GetLastMeshSyncMs();
    PhysicsTelemetryData.MeshSnapshotMs =
        GetMeshService().GetLastMeshSnapshotMs();
    PhysicsTelemetryData.MeshImmediateMs =
        GetMeshService().GetLastMeshImmediateMs();
    PhysicsTelemetryData.MeshImmediateCount =
        GetMeshService().GetLastMeshImmediateCount();
    PhysicsTelemetryData.MeshDirtyTickMs =
        GetMeshService().GetLastMeshDirtyTickMs();
  }

  if (is_moved && camera)
  {
    if (auto user = GetCurrentUser())
    {
      user->SetPosition(camera->GetPosition());
      user->SetCameraOrientation(camera->GetYaw(), camera->GetPitch());
    }
    if (ViewBinding)
    {
      ViewBinding->RefreshIntersectionFromCurrentView(*this);
    }
  }

  auto t_end = std::chrono::high_resolution_clock::now();
  DurationDoMovementMks = static_cast<uint64_t>(
      std::chrono::duration<double, std::micro>(t_end - t_begin).count());
  PhysicsTelemetryData.MovementStepMs =
      std::chrono::duration<double, std::milli>(t_after_move - t_begin).count();
  PhysicsTelemetryData.PhysicsStepMs = PhysicsTelemetryData.MovementStepMs;
  if (camera)
  {
    PhysicsTelemetryData.PhysicsSubsteps = camera->GetLastPhysicsSubsteps();
    PhysicsTelemetryData.PhysicsAccumMs =
        static_cast<double>(camera->GetPhysicsAccumulatorSec()) * 1000.0;
  }
  const double hitch_ms =
      GetWallFrameDelta() > 0.0 ? GetWallFrameDelta() * 1000.0
                                : PhysicsTelemetryData.MovementStepMs;
  SetLastMovementFrameMs(hitch_ms);
  UMovementDiagnosticsRecorder::Update(*this, camera, prevPlayerY);
}

} // namespace cutum
