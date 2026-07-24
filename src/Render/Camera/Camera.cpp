#include "Render/Camera/Camera.h"
#include "Render/Camera/CameraBasisLogic.h"
#include "Render/Engine/ViewEngine.h"
#include "Render/GlIncludes.h"
#include "World/Core/World.h"
#include <algorithm>
#include <cmath>
#if defined(__ANDROID__)
#include "App/Platform/GlfwKeyCompat.h"
#else
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#endif
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace cutum
{

float radians(float degrees)
{
  return static_cast<float>(degrees * 0.01745329251994329576923690768489);
}

UCamera::UCamera()
    : Position(glm::vec3(0.0f, 0.0f, 0.0f)),
      WorldUp(glm::vec3(0.0f, 1.0f, 0.0f)), Yaw(0.0), Pitch(0.0),
      Front(glm::vec3(0.0f, 0.0f, -1.0f)), MovementSpeed(SPEED),
      MouseSensitivity(SENSITIVTY), Zoom(ZOOM)
{
  ViewEngineInstance = nullptr;
  // Calculate aspect ratio
  AspectRatio = 1.3f;

  NearPlane = 0.1f;
  FarPlane = 512.0f;
  Fov = 90.0f;

  FreeMove = false;

  Projection = glm::mat4(1.0f);

  DeltaTime = 0.0f;
  LastFrame = std::chrono::steady_clock::now();
  LastMouseX = 0.0;
  LastMouseY = 0.0;
  FirstMouseCoords = true;

  UpdateCameraVectors();
  InitLocomotionCollisionProfile();
}

// Constructor with vectors
UCamera::UCamera(glm::vec3 position, glm::vec3 up, float yaw, float pitch)
    : Front(glm::vec3(0.0f, 0.0f, -1.0f)), MovementSpeed(SPEED),
      MouseSensitivity(SENSITIVTY), Zoom(ZOOM)
{
  ViewEngineInstance = nullptr;

  // Calculate aspect ratio
  AspectRatio = 1.3f;

  // Set near plane to 3.0, far plane to 7.0, field of view 45 degrees
  NearPlane = 0.1f;
  FarPlane = 512.0f;
  Fov = 90.0f;
  Projection = glm::mat4(1.0f);

  FreeMove = false;

  this->Position = position;
  this->WorldUp = up;
  this->Yaw = yaw;
  this->Pitch = pitch;

  DeltaTime = 0.0f;
  LastFrame = std::chrono::steady_clock::now();
  LastMouseX = 0.0;
  LastMouseY = 0.0;
  FirstMouseCoords = true;

  UpdateCameraVectors();
  InitLocomotionCollisionProfile();
}

// Constructor with scalar values
UCamera::UCamera(float posX, float posY, float posZ, float upX, float upY,
                 float upZ, float yaw, float pitch)
    : Front(glm::vec3(0.0f, 0.0f, -1.0f)), MovementSpeed(SPEED),
      MouseSensitivity(SENSITIVTY), Zoom(ZOOM)
{
  ViewEngineInstance = nullptr;

  // Calculate aspect ratio
  AspectRatio = 1.3f;

  NearPlane = 0.1f;
  FarPlane = 512.0f;
  Fov = 90.0f;
  Projection = glm::mat4(1.0f);

  FreeMove = false;

  this->Position = glm::vec3(posX, posY, posZ);
  this->WorldUp = glm::vec3(upX, upY, upZ);
  this->Yaw = yaw;
  this->Pitch = pitch;

  DeltaTime = 0.0f;
  LastFrame = std::chrono::steady_clock::now();
  LastMouseX = 0.0;
  LastMouseY = 0.0;
  FirstMouseCoords = true;

  UpdateCameraVectors();
  InitLocomotionCollisionProfile();
}

glm::mat4 UCamera::GetPose() const { return Pose; }

glm::mat4 UCamera::GetProjection() const { return Projection; }

glm::mat4 UCamera::GetViewMatrix() const { return Pose; }

glm::mat4 UCamera::GetMvpMatrix() const { return MvpMatrix; }

glm::vec3 UCamera::GetPosition() const { return Position; }

void UCamera::SetPosition(const glm::vec3 &value)
{
  Position = value;
  UpdatePose();
}

float UCamera::GetYaw() const { return Yaw; }

float UCamera::GetPitch() const { return Pitch; }

void UCamera::SetOrientation(float yaw, float pitch)
{
  Yaw = yaw;
  Pitch = pitch;
  if (Pitch > 89.0f)
  {
    Pitch = 89.0f;
  }
  if (Pitch < -89.0f)
  {
    Pitch = -89.0f;
  }
  UpdateCameraVectors();
  UpdatePose();
}

glm::vec3 UCamera::GetFront() const { return Front; }

void UCamera::SetAspectRatio(float value)
{
  AspectRatio = value;
  UpdatePose();
}

bool UCamera::GetFreeMove() const
{
  return Locomotion.GetMode() == PlayerMovementMode::Flying;
}

void UCamera::SyncFreeMoveFromController() { FreeMove = GetFreeMove(); }

void UCamera::SetFreeMove(bool value)
{
  Locomotion.SetMode(value ? PlayerMovementMode::Flying
                           : PlayerMovementMode::Walking);
  SyncFreeMoveFromController();
  UpdatePose();
}

PlayerCapsule UCamera::GetPlayerCapsule() const
{
  if (Locomotion.GetStanceBlend() > 0.05f)
  {
    return Locomotion.GetCollisionCapsule();
  }
  return Locomotion.GetCapsule();
}

float UCamera::GetAnchoredFeetY() const { return Locomotion.GetFeetY(); }

bool UCamera::HasAnchoredFeet() const { return Locomotion.IsFeetAnchored(); }

float UCamera::GetStanceBlend() const { return Locomotion.GetStanceBlend(); }

bool UCamera::IsCrouching() const { return Locomotion.GetStanceBlend() > 0.5f; }

bool UCamera::IsOnGround() const { return Locomotion.IsOnGround(); }

bool UCamera::IsShiftDown() const
{
  const auto Pressed = [](const std::map<size_t, bool> &keys, size_t code)
  {
    const auto it = keys.find(code);
    return it != keys.end() && it->second;
  };
  return Pressed(KeysStatus, GLFW_KEY_LEFT_SHIFT) ||
         Pressed(KeysStatus, GLFW_KEY_RIGHT_SHIFT);
}

void UCamera::ClearShiftKeyState()
{
  KeysStatus[GLFW_KEY_LEFT_SHIFT] = false;
  KeysStatus[GLFW_KEY_RIGHT_SHIFT] = false;
}

bool UCamera::OnSpacePressed()
{
  const bool toggled = Locomotion.OnSpacePressed();
  SyncFreeMoveFromController();
  return toggled;
}

bool UCamera::TryToggleFlightOnDoubleSpace() { return OnSpacePressed(); }

PlayerInput UCamera::BuildPlayerInput(bool spaceJustPressed) const
{
  PlayerInput input;
  const auto keyDown = [this](size_t key)
  {
    const auto it = KeysStatus.find(key);
    return it != KeysStatus.end() && it->second;
  };
  input.MoveForward = keyDown(GLFW_KEY_W);
  input.MoveBack = keyDown(GLFW_KEY_S);
  input.MoveLeft = keyDown(GLFW_KEY_A);
  input.MoveRight = keyDown(GLFW_KEY_D);
  input.jumpHeld = keyDown(GLFW_KEY_SPACE);
  input.jumpPressed = spaceJustPressed;
  input.crouchHeld = IsShiftDown();
  input.sprintHeld = SprintActive || keyDown(GLFW_KEY_LEFT_CONTROL) ||
                     keyDown(GLFW_KEY_RIGHT_CONTROL);
  return input;
}

PlayerInput UCamera::GetMovementInput() const
{
  return BuildPlayerInput(false);
}

void UCamera::SetViewEngine(UViewEngine *view_engine)
{
  ViewEngineInstance = view_engine;
}

glm::vec3 UCamera::ComputeHorizontalShift(float deltaTime)
{
  const PlayerInput input = BuildPlayerInput(false);
  const float velocity = Locomotion.ResolveHorizontalSpeed(input) * deltaTime;
  glm::vec3 shift(0.0f);
  if (KeysStatus[GLFW_KEY_W])
  {
    shift += glm::vec3(std::cos(radians(Yaw)), 0.0f, std::sin(radians(Yaw))) *
             velocity;
  }
  if (KeysStatus[GLFW_KEY_S])
  {
    shift -= glm::vec3(std::cos(radians(Yaw)), 0.0f, std::sin(radians(Yaw))) *
             velocity;
  }
  if (KeysStatus[GLFW_KEY_A])
  {
    shift -= Right * velocity;
  }
  if (KeysStatus[GLFW_KEY_D])
  {
    shift += Right * velocity;
  }
  return shift;
}

void UCamera::UpdateMoveIntentFromKeys()
{
  glm::vec3 shift = ComputeHorizontalShift(1.0f);
  shift.y = 0.0f;
  if (glm::dot(shift, shift) < 1e-10f)
  {
    return;
  }
  LastMoveIntentDir = shift / std::sqrt(glm::dot(shift, shift));
  LastMoveIntentValid = true;
  LastMoveIntentTime = std::chrono::steady_clock::now();
}

glm::vec3 UCamera::GetMoveIntentDir() const
{
  auto keyDown = [this](size_t key)
  {
    const auto it = KeysStatus.find(key);
    return it != KeysStatus.end() && it->second;
  };
  glm::vec3 shift = glm::vec3(0.0f);
  if (keyDown(GLFW_KEY_W))
  {
    shift += glm::vec3(std::cos(radians(Yaw)), 0.0f, std::sin(radians(Yaw)));
  }
  if (keyDown(GLFW_KEY_S))
  {
    shift -= glm::vec3(std::cos(radians(Yaw)), 0.0f, std::sin(radians(Yaw)));
  }
  if (keyDown(GLFW_KEY_A))
  {
    shift -= Right;
  }
  if (keyDown(GLFW_KEY_D))
  {
    shift += Right;
  }
  shift.y = 0.0f;
  if (glm::dot(shift, shift) > 1e-10f)
  {
    return shift / std::sqrt(glm::dot(shift, shift));
  }
  if (LastMoveIntentValid)
  {
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - LastMoveIntentTime)
                        .count();
    if (ms >= 0 &&
        ms < static_cast<long long>(kStepUpIntentRetainSec * 1000.0f))
    {
      return LastMoveIntentDir;
    }
  }
  return glm::vec3(0.0f);
}

bool UCamera::TickStepUpAnimation(const UWorld *world, float dt)
{
  if (!StepUpAnim.Active)
  {
    return false;
  }

  StepUpAnim.Elapsed += dt;
  float t = StepUpAnim.Elapsed / kStepUpAnimDuration;
  if (t >= 1.0f)
  {
    Position = StepUpAnim.TargetPos;
    StepUpAnim.Active = false;
    Locomotion.SyncAfterStepLanding(Position, world);
    UpdatePose();
    return true;
  }

  t = std::min(1.0f, t);
  const float ease = 1.0f - (1.0f - t) * (1.0f - t);
  const float arc = std::sin(t * 3.14159265f) * 0.14f;
  glm::vec3 desired =
      StepUpAnim.StartPos + (StepUpAnim.TargetPos - StepUpAnim.StartPos) * ease;
  desired.y += arc;
  Position = desired;
  UpdatePose();
  return true;
}

bool UCamera::ApplyHorizontalMovement(const UWorld *world, float deltaTime)
{
  if (TickStepUpAnimation(world, deltaTime))
  {
    return true;
  }

  UpdateMoveIntentFromKeys();

  glm::vec3 shift = ComputeHorizontalShift(deltaTime);
  const bool hasShift = glm::dot(shift, shift) > 1e-10f;

  const PlayerCapsule cap = GetPlayerCapsule();
  const SampledFluidState fluid = world->SampleFluidPhysics(Position, cap);
  if (hasShift && fluid.inFluid)
  {
    const float drag =
        1.0f - std::min(0.95f, fluid.DragHorizontal * deltaTime * 8.0f);
    shift.x *= drag;
    shift.z *= drag;
  }

  glm::vec3 newPos = Position;
  if (hasShift)
  {
    newPos = world->ResolveMovement(Position, shift, cap,
                                    world->GetMovementCollisionSkipId());
  }

  const bool grounded =
      world->HasGroundSupport(Position, cap) || Locomotion.IsOnGround();
  const glm::vec3 intent = GetMoveIntentDir();
  const PlayerInput stepInput = BuildPlayerInput(false);

  bool stepped = false;
  if (world->IsStepUpEnabled() && !fluid.inFluid && grounded &&
      Locomotion.IsOnGround() && Locomotion.IsFeetAnchored() &&
      !stepInput.jumpHeld && Locomotion.GetVerticalVelocity() <= 0.05f &&
      glm::dot(intent, intent) > 1e-10f)
  {
    const UWorld::StepUpProbe probe =
        world->ProbeStepUp(newPos, intent, cap, kStepUpTriggerDistance);
    if (probe.Valid)
    {
      glm::vec3 landing = newPos;
      if (!world->GetStepUpLanding(newPos, intent, cap, kStepUpTriggerDistance,
                                   landing))
      {
        landing = probe.TargetPos - glm::vec3(probe.MoveDir.x * 0.18f, 0.0f,
                                              probe.MoveDir.z * 0.18f);
      }
      StepUpAnim.Active = true;
      StepUpAnim.StartPos = newPos;
      StepUpAnim.TargetPos = landing;
      StepUpAnim.Elapsed = 0.0f;
      stepped = true;
    }
  }

  if (!stepped)
  {
    Position = newPos;
  }
  else
  {
    TickStepUpAnimation(world, deltaTime);
  }
  UpdatePose();
  return hasShift || stepped;
}

// Processes input received from any keyboard-like input system. Accepts input
// parameter in the form of camera defined ENUM (to abstract it from windowing
// systems)
void UCamera::ProcessKeyboard(const UWorld *world, Camera_Movement direction,
                              float deltaTime,
                              const PlayerCapsule &collisionCap)
{
  const PlayerInput input = BuildPlayerInput(false);
  const float speed = Locomotion.ResolveHorizontalSpeed(input);
  const float velocity = speed * deltaTime;
  glm::vec3 shift(0.0f);

  if (direction == FORWARD)
  {
    shift += FreeMove ? Front * velocity
                      : glm::vec3(std::cos(radians(Yaw)), 0.0f,
                                  std::sin(radians(Yaw))) *
                            velocity;
  }
  else if (direction == BACKWARD)
  {
    shift -= FreeMove ? Front * velocity
                      : glm::vec3(std::cos(radians(Yaw)), 0.0f,
                                  std::sin(radians(Yaw))) *
                            velocity;
  }
  else if (direction == LEFT)
  {
    shift -= Right * velocity;
  }
  else if (direction == RIGHT)
  {
    shift += Right * velocity;
  }
  else if (direction == UP)
  {
    shift += Up * velocity;
  }
  else if (direction == DOWN)
  {
    shift -= Up * velocity;
  }

  if (glm::dot(shift, shift) < 1e-10f)
  {
    return;
  }

  if (world)
  {
    glm::vec3 newPos = world->ResolveMovement(
        Position, shift, collisionCap, world->GetMovementCollisionSkipId());
    Position = newPos;
  }
  else
  {
    Position += shift;
  }
  UpdatePose();
}

// Processes input received from a mouse input system. Expects the offset value
// in both the x and y direction.
void UCamera::ProcessMouseMovement(float Xoffset, float Yoffset,
                                   bool constrainPitch)
{
  Xoffset *= this->MouseSensitivity;
  Yoffset *= this->MouseSensitivity;

  this->Yaw += Xoffset;
  this->Pitch += Yoffset;

  // Make sure that when pitch is out of bounds, screen doesn't get flipped
  if (constrainPitch)
  {
    if (this->Pitch > 89.0f)
      this->Pitch = 89.0f;
    if (this->Pitch < -89.0f)
      this->Pitch = -89.0f;
  }

  // Update Front, Right and Up Vectors using the updated Eular angles
  UpdateCameraVectors();
}

// Processes input received from a mouse scroll-wheel event. Only requires input
// on the Vertical wheel-axis
void UCamera::ProcessMouseScroll(float Yoffset)
{
  if (this->Zoom >= 1.0f && this->Zoom <= 45.0f)
    this->Zoom -= Yoffset;
  if (this->Zoom <= 1.0f)
    this->Zoom = 1.0f;
  if (this->Zoom >= 45.0f)
    this->Zoom = 45.0f;
  UpdatePose();
}

void UCamera::UpdateCameraVectors()
{
  ComputeFpsCameraBasis(static_cast<float>(Yaw), static_cast<float>(Pitch),
                        Front, Right, Up, WorldUp);
  UpdatePose();
}

glm::vec3 UCamera::ComputeCameraWorldPosition() const
{
  if (Perspective == CameraPerspective::FirstPerson)
  {
    return Position;
  }
  if (Perspective == CameraPerspective::ThirdPersonBack)
  {
    return Position - Front * ThirdPersonDistance + Up * ThirdPersonHeight;
  }
  return Position + Front * ThirdPersonDistance;
}

void UCamera::CyclePerspective()
{
  Perspective = CycleCameraPerspective(Perspective);
  UpdatePose();
}

void UCamera::UpdatePose()
{
  const glm::vec3 eye = Position;
  const glm::vec3 camWorld = ComputeCameraWorldPosition();
  const glm::vec3 target = eye + Front;
  Pose = glm::lookAt(camWorld, target, Up);

  Projection =
      glm::perspective(glm::radians(Fov), AspectRatio, NearPlane, FarPlane);

  MvpMatrix = Projection * Pose;
}

void UCamera::UpdateKeyStatus(size_t key_index, bool is_pressed)
{
  KeysStatus[key_index] = is_pressed;
}

void UCamera::ResetAllKeyStatus()
{
  for (auto I = KeysStatus.begin(); I != KeysStatus.end(); ++I)
    I->second = false;
  SprintActive = false;
}

void UCamera::UpdateMouseMove(std::shared_ptr<UWorld> world, double xpos,
                              double ypos)
{
  if (FirstMouseCoords)
  {
    LastMouseX = xpos;
    LastMouseY = ypos;
    FirstMouseCoords = false;
  }

  const double Xoffset = xpos - LastMouseX;
  const double Yoffset =
      LastMouseY - ypos; // Reversed since y-coordinates go from bottom to left

  LastMouseX = xpos;
  LastMouseY = ypos;

  ProcessMouseMovement(static_cast<float>(Xoffset),
                       static_cast<float>(Yoffset));
  world->UpdateIntersection(GetPosition(), GetFront());
}

void UCamera::ResetMouseMove(double xpos, double ypos)
{
  LastMouseX = xpos;
  LastMouseY = ypos;
  FirstMouseCoords = true;
}

void UCamera::ApplyRelativeMouseMove(float Xoffset, float Yoffset)
{
  ProcessMouseMovement(Xoffset, Yoffset);
}

void UCamera::UpdateMouseScroll(double Xoffset, double Yoffset)
{
  ProcessMouseScroll(static_cast<float>(Yoffset));
}

void UCamera::UpdateFrameTime()
{
  auto current_frame = std::chrono::steady_clock::now();
  DeltaTime = static_cast<float>(
      std::chrono::duration<double>(current_frame - LastFrame).count());
  LastFrame = current_frame;
}

void UCamera::InitLocomotionCollisionProfile()
{
  const PlayerCapsule stand = PlayerCapsule::Standing();
  Locomotion.SetCollisionProfile(
      glm::vec3(stand.halfWidth * 2.0f, stand.height, stand.halfWidth * 2.0f),
      stand.eyeHeight);
}

void UCamera::ApplyCreatureLocomotion(
    const CreatureLocomotionCapabilities &caps,
    const CreatureBoundsProfile &bounds, float eyeHeight)
{
  Locomotion.SetCapabilities(caps);
  const glm::vec3 size = bounds.restSizeBlocks.x > 0.0f
                             ? bounds.restSizeBlocks
                             : glm::vec3(0.6f, 1.8f, 0.6f);
  Locomotion.SetCollisionProfile(size, eyeHeight);
  MovementSpeed = caps.walkSpeed;
}

void UCamera::ResetVerticalPhysics()
{
  Locomotion.Reset();
  InitLocomotionCollisionProfile();
  StepUpAnim.Active = false;
  PhysicsAccumulator = 0.0f;
  SyncFreeMoveFromController();
  UpdatePose();
}

void UCamera::SuspendFallThroughUnloadedChunks()
{
  if (GetFreeMove())
  {
    return;
  }
  Locomotion.ClampVerticalVelocity(0.0f);
}

bool UCamera::DoMovement(const UWorld *world)
{
  const float frameDt = std::min(static_cast<float>(DeltaTime), kMaxFrameDelta);
  const PlayerCapsule flightCap = PlayerCapsule::Standing();
  if (!GetFreeMove() && Locomotion.ConsumeClearShiftRequest())
  {
    ClearShiftKeyState();
  }
  const PlayerInput input = BuildPlayerInput(false);

  bool is_moved(false);
  SyncFreeMoveFromController();
  LastPhysicsSubsteps = 0;

  if (GetFreeMove())
  {
    PhysicsAccumulator += frameDt;
    while (PhysicsAccumulator >= kFixedPhysicsDt &&
           LastPhysicsSubsteps < kMaxPhysicsSubsteps)
    {
      PhysicsAccumulator -= kFixedPhysicsDt;
      ++LastPhysicsSubsteps;
      const float dt = kFixedPhysicsDt;
      const bool groundedInFlight =
          world && world->HasGroundSupport(Position, flightCap);
      if (groundedInFlight)
      {
        if (IsShiftDown())
        {
          ClearShiftKeyState();
        }
        Locomotion.OnLandedFromFlight(world, Position, false);
        SyncFreeMoveFromController();
        break;
      }
      if (KeysStatus[GLFW_KEY_W])
      {
        ProcessKeyboard(world, FORWARD, dt, flightCap);
        is_moved = true;
      }
      if (KeysStatus[GLFW_KEY_S])
      {
        ProcessKeyboard(world, BACKWARD, dt, flightCap);
        is_moved = true;
      }
      if (KeysStatus[GLFW_KEY_A])
      {
        ProcessKeyboard(world, LEFT, dt, flightCap);
        is_moved = true;
      }
      if (KeysStatus[GLFW_KEY_D])
      {
        ProcessKeyboard(world, RIGHT, dt, flightCap);
        is_moved = true;
      }
      if (KeysStatus[GLFW_KEY_SPACE])
      {
        ProcessKeyboard(world, UP, dt, flightCap);
        is_moved = true;
      }
      if (IsShiftDown() && world &&
          !world->HasGroundSupport(Position, flightCap))
      {
        ProcessKeyboard(world, DOWN, dt, flightCap);
        is_moved = true;
      }
      if (!input.jumpHeld)
      {
        Locomotion.NotifySpaceReleased();
      }
    }
  }
  else if (world)
  {
    PhysicsAccumulator += frameDt;
    while (PhysicsAccumulator >= kFixedPhysicsDt &&
           LastPhysicsSubsteps < kMaxPhysicsSubsteps)
    {
      PhysicsAccumulator -= kFixedPhysicsDt;
      ++LastPhysicsSubsteps;

      if (IsStepUpAnimationActive())
      {
        if (TickStepUpAnimation(world, kFixedPhysicsDt))
        {
          is_moved = true;
        }
        UpdatePose();
        continue;
      }

      if (ApplyHorizontalMovement(world, kFixedPhysicsDt))
      {
        is_moved = true;
      }

      if (Position.y < kMinReasonablePlayerY)
      {
        break;
      }

      Locomotion.UpdateLocomotion(world, Position, input, kFixedPhysicsDt,
                                  world->GetMovementCollisionSkipId());
      is_moved = true;
      UpdatePose();
    }
  }

  return is_moved;
}

} // namespace cutum
