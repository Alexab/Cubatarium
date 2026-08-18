#include "Render/Camera/Camera.h"
#include "Render/Camera/CameraBasisLogic.h"
#include "Render/Camera/CameraLens.h"
#include "Render/Camera/Control/IUGameplayViewController.h"
#include "Render/Engine/ViewEngine.h"
#include "Creatures/Locomotion/CreatureMotor.h"
#include "World/View/ViewRayMath.h"
#include "Render/GlIncludes.h"
#include "World/Core/World.h"
#include "World/Streaming/PhysicsStepPolicy.h"
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

glm::vec3 UCamera::GetRight() const { return Right; }

glm::vec3 UCamera::GetUp() const { return Up; }

glm::vec3 UCamera::GetWorldUp() const { return WorldUp; }

const IUGameplayViewController &UCamera::GetViewController() const
{
  return GameplayViewControllerFor(IsIsometricProjection());
}

CreatureViewOrientation UCamera::ResolveCreatureViewOrientation() const
{
  return GetViewController().ResolveCreatureOrientation(*this,
                                                        GetMoveIntentDir());
}

void UCamera::SetAspectRatio(float value)
{
  AspectRatio = value;
  UpdatePose();
}

void UCamera::SetViewportSize(int width, int height)
{
  ViewportWidth = std::max(width, 0);
  ViewportHeight = std::max(height, 0);
  if (ViewportHeight > 0)
  {
    AspectRatio =
        static_cast<float>(ViewportWidth) / static_cast<float>(ViewportHeight);
  }
  UpdatePose();
}

bool UCamera::TryGetCenterViewRay(glm::vec3 &out_origin,
                                  glm::vec3 &out_dir) const
{
  if (IsIsometricProjection() && PointerScreenValid)
  {
    return TryGetViewRayAtScreen(PointerScreenX, PointerScreenY, out_origin,
                                 out_dir);
  }
  if (ViewportWidth <= 0 || ViewportHeight <= 0)
  {
    if (IsIsometricProjection())
    {
      out_origin = GetViewController().ComputeCameraWorldPosition(*this);
      const glm::vec3 target = GetViewController().ComputeLookTarget(*this);
      out_dir = target - out_origin;
      const float len = glm::length(out_dir);
      if (len > 1.0e-6f)
      {
        out_dir /= len;
        return true;
      }
    }
    out_origin = Position;
    out_dir = Front;
    return true;
  }
  const float mouse_x = static_cast<float>(ViewportWidth) * 0.5f;
  const float mouse_y = static_cast<float>(ViewportHeight) * 0.5f;
  return TryGetViewRayAtScreen(mouse_x, mouse_y, out_origin, out_dir);
}

bool UCamera::TryGetViewRayAtScreen(float screen_x, float screen_y,
                                    glm::vec3 &out_origin,
                                    glm::vec3 &out_dir) const
{
  if (ViewportWidth <= 0 || ViewportHeight <= 0)
  {
    if (IsIsometricProjection())
    {
      out_origin = GetViewController().ComputeCameraWorldPosition(*this);
      const glm::vec3 target = GetViewController().ComputeLookTarget(*this);
      out_dir = target - out_origin;
      const float len = glm::length(out_dir);
      if (len > 1.0e-6f)
      {
        out_dir /= len;
        return true;
      }
    }
    out_origin = Position;
    out_dir = Front;
    return true;
  }

  // GLFW/cursor Y is top-left; glm::unProject expects bottom-left.
  const float gl_y =
      static_cast<float>(ViewportHeight) - screen_y;
  const glm::ivec4 viewport(0, 0, ViewportWidth, ViewportHeight);
  if (!ScreenPointToWorldRay(Pose, Projection, viewport, screen_x, gl_y,
                             out_origin, out_dir))
  {
    if (IsIsometricProjection())
    {
      out_origin = GetViewController().ComputeCameraWorldPosition(*this);
      const glm::vec3 target = GetViewController().ComputeLookTarget(*this);
      out_dir = target - out_origin;
      const float len = glm::length(out_dir);
      if (len > 1.0e-6f)
      {
        out_dir /= len;
        return true;
      }
    }
    out_origin = Position;
    out_dir = Front;
  }
  return true;
}

void UCamera::SetPointerScreenPos(float screen_x, float screen_y)
{
  PointerScreenX = screen_x;
  PointerScreenY = screen_y;
  PointerScreenValid = true;
}

void UCamera::RotateIsoYaw(int delta_steps)
{
  if (!IsIsometricProjection())
  {
    return;
  }
  SetIsoYawIndex(IsoYawIndex + delta_steps);
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
  const bool moving = input.MoveForward || input.MoveBack || input.MoveLeft ||
                      input.MoveRight;
  if (!moving)
  {
    return glm::vec3(0.0f);
  }
  const float velocity = Locomotion.ResolveHorizontalSpeed(input) * deltaTime;
  const glm::vec3 intent = GetMoveIntentDir();
  if (glm::dot(intent, intent) < 1e-10f)
  {
    return glm::vec3(0.0f);
  }
  return intent * velocity;
}

void UCamera::UpdateMoveIntentFromKeys()
{
  const PlayerInput input = BuildPlayerInput(false);
  const bool moving = input.MoveForward || input.MoveBack || input.MoveLeft ||
                      input.MoveRight;
  if (!moving)
  {
    return;
  }
  float forward = 0.0f;
  float rightward = 0.0f;
  if (input.MoveForward)
  {
    forward += 1.0f;
  }
  if (input.MoveBack)
  {
    forward -= 1.0f;
  }
  if (input.MoveLeft)
  {
    rightward -= 1.0f;
  }
  if (input.MoveRight)
  {
    rightward += 1.0f;
  }
  glm::vec3 shift =
      GetViewController().ProjectMoveIntent(*this, forward, rightward);
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
  float forward = 0.0f;
  float rightward = 0.0f;
  if (keyDown(GLFW_KEY_W))
  {
    forward += 1.0f;
  }
  if (keyDown(GLFW_KEY_S))
  {
    forward -= 1.0f;
  }
  if (keyDown(GLFW_KEY_A))
  {
    rightward -= 1.0f;
  }
  if (keyDown(GLFW_KEY_D))
  {
    rightward += 1.0f;
  }

  glm::vec3 shift = glm::vec3(0.0f);
  if (forward != 0.0f || rightward != 0.0f)
  {
    shift = GetViewController().ProjectMoveIntent(*this, forward, rightward);
  }

  if (glm::dot(shift, shift) > 1e-10f)
  {
    return shift;
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

  const PlayerInput stepInput = BuildPlayerInput(false);
  const bool moving = stepInput.MoveForward || stepInput.MoveBack ||
                      stepInput.MoveLeft || stepInput.MoveRight;
  glm::vec3 wish(0.0f);
  float speed = 0.0f;
  if (moving)
  {
    wish = GetMoveIntentDir();
    speed = Locomotion.ResolveHorizontalSpeed(stepInput);
  }

  const CreatureMotorHorizontalResult motor = ApplyCreatureMotorHorizontal(
      *world, Position, Locomotion, wish, speed, deltaTime,
      world->GetMovementCollisionSkipId(), world->IsStepUpEnabled(),
      stepInput.jumpHeld, false);

  bool stepped = false;
  if (motor.wantsStepUpAnim)
  {
    StepUpAnim.Active = true;
    StepUpAnim.StartPos = motor.stepUpStart;
    StepUpAnim.TargetPos = motor.stepUpTarget;
    StepUpAnim.Elapsed = 0.0f;
    stepped = true;
    TickStepUpAnimation(world, deltaTime);
  }
  else
  {
    Position = motor.eyePos;
  }
  UpdatePose();
  return motor.moved || stepped;
}

// Processes input received from any keyboard-like input system. Accepts input
// parameter in the form of camera defined ENUM (to abstract it from windowing
// systems)
void UCamera::ProcessKeyboard(const UWorld *world, Camera_Movement direction,
                              float deltaTime,
                              const PlayerCapsule &collisionCap)
{
  const PlayerInput input = BuildPlayerInput(false);
  float speed = Locomotion.ResolveHorizontalSpeed(input);
  // P3: soft streaming integrity clamp (underfeet / near ahead miss).
  if (world)
  {
    const float clamp =
        world->GetPhysicsTelemetry().StreamSpeedClampScale;
    if (clamp > 0.0f && clamp < 1.0f)
    {
      speed *= clamp;
    }
  }
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
void UCamera::ApplyFpsLookDelta(float Xoffset, float Yoffset,
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

void UCamera::ApplyFpsZoomScroll(float Yoffset)
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

void UCamera::CycleFpsPerspective()
{
  Perspective = CycleCameraPerspective(Perspective);
  UpdatePose();
}

void UCamera::CyclePerspective()
{
  GetViewController().CycleView(*this);
}

void UCamera::SetProjectionMode(ProjectionMode mode)
{
  Mode = mode;
  if (IsIsometricProjection())
  {
    Perspective = CameraPerspective::ThirdPersonBack;
    AimYawDeg = Yaw;
    IsoOrbitYawDeg =
        -90.0f + 45.0f + static_cast<float>(IsoYawIndex) * 90.0f;
    ApplyIsometricOrientation();
  }
  UpdatePose();
}

void UCamera::SetOrthoSize(float value)
{
  OrthoSize = std::clamp(value, 4.0f, 256.0f);
  UpdatePose();
}

void UCamera::SetIsoYawIndex(int index)
{
  IsoYawIndex = ((index % 4) + 4) % 4;
  IsoOrbitYawDeg =
      -90.0f + 45.0f + static_cast<float>(IsoYawIndex) * 90.0f;
  if (IsIsometricProjection())
  {
    ApplyIsometricOrientation();
  }
  UpdatePose();
}

void UCamera::SetIsoPitchDeg(float degrees)
{
  IsoPitchDeg = std::clamp(degrees, 15.0f, 60.0f);
  if (IsIsometricProjection())
  {
    ApplyIsometricOrientation();
  }
  UpdatePose();
}

void UCamera::SetIsoViewPreset(IsoViewPreset preset)
{
  IsoBoomPreset = preset;
  UpdatePose();
}

float UCamera::GetIsoBoomDistance() const
{
  return IsoBoomDistanceForPreset(IsoBoomPreset);
}

float UCamera::GetBlockInteractMaxDistance() const
{
  if (IsIsometricProjection())
  {
    // View ray starts at the elevated camera near plane, not at the player eye.
    return GetIsoBoomDistance() + GetOrthoSize() + 16.0f;
  }
  return 8.0f;
}

void UCamera::SetAimYawDeg(float yaw_deg)
{
  AimYawDeg = yaw_deg;
}

void UCamera::AddAimYawDeg(float delta_deg)
{
  AimYawDeg += delta_deg;
}

void UCamera::SetIsoOrbitYawDeg(float yaw_deg)
{
  IsoOrbitYawDeg = yaw_deg;
  if (IsIsometricProjection())
  {
    ApplyIsometricOrientation();
  }
  else
  {
    UpdatePose();
  }
}

void UCamera::AddIsoOrbitYawDeg(float delta_deg)
{
  SetIsoOrbitYawDeg(IsoOrbitYawDeg + delta_deg);
}

void UCamera::SnapIsoCameraYaw(int delta_steps)
{
  GetViewController().SnapCameraYaw(*this, delta_steps);
}

void UCamera::ApplyIsometricOrientation()
{
  // Continuous orbit yaw; pitch looks down at the world.
  Yaw = IsoOrbitYawDeg;
  Pitch = -IsoPitchDeg;
  UpdateCameraVectors();
}

void UCamera::ApplyWorldViewSettings(const WorldViewSettings &settings)
{
  WorldViewSettings validated = settings;
  validated.Validate();
  Mode = ProjectionModeFromWorld(validated.Projection);
  OrthoSize = validated.OrthoSize;
  IsoYawIndex = validated.IsoYawIndex;
  IsoPitchDeg = validated.IsoPitchDeg;
  IsoBoomPreset = validated.IsoBoomPreset;
  if (IsIsometricProjection())
  {
    Perspective = CameraPerspective::ThirdPersonBack;
    AimYawDeg = Yaw;
    IsoOrbitYawDeg =
        -90.0f + 45.0f + static_cast<float>(IsoYawIndex) * 90.0f;
    ApplyIsometricOrientation();
  }
  else
  {
    UpdatePose();
  }
}

WorldViewSettings UCamera::CaptureWorldViewSettings() const
{
  WorldViewSettings settings;
  settings.Projection = WorldProjectionModeFromProjection(Mode);
  settings.OrthoSize = OrthoSize;
  settings.IsoYawIndex = IsoYawIndex;
  settings.IsoPitchDeg = IsoPitchDeg;
  settings.IsoBoomPreset = IsoBoomPreset;
  settings.Validate();
  return settings;
}

void UCamera::UpdatePose()
{
  const IUGameplayViewController &controller = GetViewController();
  const glm::vec3 camWorld = controller.ComputeCameraWorldPosition(*this);
  const glm::vec3 target = controller.ComputeLookTarget(*this);
  const glm::vec3 up =
      IsIsometricProjection() ? WorldUp : Up;
  Pose = glm::lookAt(camWorld, target, up);

  if (Mode == ProjectionMode::OrthographicIsometric)
  {
    Projection = CameraLens::BuildIsometricOrtho(OrthoSize, AspectRatio,
                                                 NearPlane, FarPlane);
  }
  else
  {
    Projection = CameraLens::BuildPerspective(Fov, AspectRatio, NearPlane,
                                              FarPlane);
  }

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

  ApplyRelativeMouseMove(static_cast<float>(Xoffset),
                         static_cast<float>(Yoffset));
  glm::vec3 ray_origin;
  glm::vec3 ray_dir;
  if (IsIsometricProjection())
  {
    SetPointerScreenPos(static_cast<float>(xpos), static_cast<float>(ypos));
  }
  TryGetCenterViewRay(ray_origin, ray_dir);
  world->UpdateIntersection(ray_origin, ray_dir);
}

void UCamera::UpdatePointerAim(std::shared_ptr<UWorld> world, double xpos,
                               double ypos)
{
  SetPointerScreenPos(static_cast<float>(xpos), static_cast<float>(ypos));
  LastMouseX = xpos;
  LastMouseY = ypos;
  FirstMouseCoords = false;
  glm::vec3 ray_origin;
  glm::vec3 ray_dir;
  TryGetCenterViewRay(ray_origin, ray_dir);
  world->UpdateIntersection(ray_origin, ray_dir);
}

void UCamera::ResetMouseMove(double xpos, double ypos)
{
  LastMouseX = xpos;
  LastMouseY = ypos;
  FirstMouseCoords = true;
  if (IsIsometricProjection())
  {
    SetPointerScreenPos(static_cast<float>(xpos), static_cast<float>(ypos));
  }
}

void UCamera::ApplyRelativeMouseMove(float Xoffset, float Yoffset)
{
  GetViewController().ApplyLookDelta(*this, Xoffset, Yoffset);
}

void UCamera::UpdateMouseScroll(double Xoffset, double Yoffset)
{
  (void)Xoffset;
  GetViewController().ApplyScroll(*this, static_cast<float>(Yoffset));
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
  LastGroundSupportMs = 0.0;
  LastLocomotionMs = 0.0;
  LastHorizMoveMs = 0.0;
  int substep_cap = kMaxPhysicsSubsteps;
  if (world)
  {
    const PhysicsTelemetry &phys = world->GetPhysicsTelemetry();
    substep_cap = std::min(kMaxPhysicsSubsteps,
                           PhysicsSubstepCap(IsStreamingPhysicsRed(
                               phys.PhaseBudgetOver != 0,
                               phys.FocusMissingMesh != 0,
                               world->GetWallFrameDelta() * 1000.0)));
  }

  if (GetFreeMove())
  {
    PhysicsAccumulator += frameDt;
    while (PhysicsAccumulator >= kFixedPhysicsDt &&
           LastPhysicsSubsteps < substep_cap)
    {
      PhysicsAccumulator -= kFixedPhysicsDt;
      ++LastPhysicsSubsteps;
      const float dt = kFixedPhysicsDt;
      const auto tgs0 = std::chrono::high_resolution_clock::now();
      const bool groundedInFlight =
          world && world->HasGroundSupport(Position, flightCap);
      LastGroundSupportMs += std::chrono::duration<double, std::milli>(
          std::chrono::high_resolution_clock::now() - tgs0).count();
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
           LastPhysicsSubsteps < substep_cap)
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

      {
        const auto th0 = std::chrono::high_resolution_clock::now();
        if (ApplyHorizontalMovement(world, kFixedPhysicsDt))
        {
          is_moved = true;
        }
        LastHorizMoveMs += std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - th0).count();
      }

      if (Position.y < kMinReasonablePlayerY)
      {
        break;
      }

      {
        const auto tl0 = std::chrono::high_resolution_clock::now();
        Locomotion.UpdateLocomotion(world, Position, input, kFixedPhysicsDt,
                                    world->GetMovementCollisionSkipId());
        LastLocomotionMs += std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - tl0).count();
      }
      is_moved = true;
      UpdatePose();
    }
  }

  if (LastPhysicsSubsteps >= substep_cap &&
      PhysicsAccumulator >= kFixedPhysicsDt)
  {
    PhysicsAccumulator = 0.0f;
  }

  return is_moved;
}

} // namespace cutum
