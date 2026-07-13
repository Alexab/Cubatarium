#include "World/Core/WorldViewBinding.h"

#include "Activity/WorldCreatureActivitySink.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureBounds.h"
#include "Creatures/Locomotion/CreatureLocomotionController.h"
#include "Creatures/Locomotion/LocomotionTypes.h"
#include "Creatures/Player/PlayerCapsule.h"
#include "Creatures/Player/User.h"
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
  const PlayerCapsule cap = PlayerCapsule::Standing();
  world.DepenetrateEye(spawn, cap, world.GetMovementCollisionSkipId());
  world.SpawnPoint = spawn;

  if (auto user = world.GetCurrentUser())
  {
    user->SetPosition(world.SpawnPoint);
    user->SetCameraOrientation(-90.0f, 0.0f);
  }
  if (auto camera = GetCurrentUserCamera(world))
  {
    camera->SetPosition(world.SpawnPoint);
    camera->SetOrientation(-90.0f, 0.0f);
    return;
  }
  if (Engine_ && Engine_->GetActiveCamera())
  {
    Engine_->GetActiveCamera()->SetPosition(world.SpawnPoint);
    Engine_->GetActiveCamera()->SetOrientation(-90.0f, 0.0f);
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

  const PlayerCapsule cap = PlayerCapsule::Standing();
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
  position = camera->GetPosition();
  front = camera->GetFront();
  return true;
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
  const BlockPlacementResolve resolved =
      Collision.ResolveBlockPlacement(position, front, cap, 8.0f);

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
        FeetYFromEye(eyePos, controlled ? controlled->GetEyeOffset().y : 1.62f);
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
        if (Environment.GetControlledCreatureId() != 0 &&
            creature.GetId() == Environment.GetControlledCreatureId())
        {
          return;
        }
        if (creature.IsPossessed())
        {
          return;
        }
        creature.ExecuteIntent(*this, dt);
      });

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
    controlled->SetOrientation(ModelYawFromCameraYaw(camera->GetYaw()),
                               camera->GetPitch());
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
    controlled->RebuildLocomotionFactsFromController(
        camLoc, controlled->GetLocomotion().GetCapabilities(),
        static_cast<float>(camera->GetDeltaTime()), horizontalSpeed, this);
    is_moved = true;
  }

  if (camera)
  {
    UpdateStreaming();
    TickAsyncChunkSystems();
    TickMeshEmerge();
    BlockWorldReady = true;
  }

  if (is_moved && camera)
  {
    if (auto user = GetCurrentUser())
    {
      user->SetPosition(camera->GetPosition());
      user->SetCameraOrientation(camera->GetYaw(), camera->GetPitch());
    }
    UpdateIntersection(camera->GetPosition(), camera->GetFront());
  }

  auto t_end = std::chrono::high_resolution_clock::now();
  DurationDoMovementMks = static_cast<uint64_t>(
      std::chrono::duration<double, std::micro>(t_end - t_begin).count());
  PhysicsTelemetryData.MovementStepMs =
      std::chrono::duration<double, std::milli>(t_end - t_begin).count();
  SetLastMovementFrameMs(PhysicsTelemetryData.MovementStepMs);
  UMovementDiagnosticsRecorder::Update(*this, camera, prevPlayerY);
}

} // namespace cutum
