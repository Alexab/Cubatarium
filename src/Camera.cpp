#include <cmath>
#include <algorithm>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "Camera.h"
#include "ViewEngine.h"
#include "World.h"


namespace cutum {

float radians(float degrees)
{
 return static_cast<float>(degrees * 0.01745329251994329576923690768489);
}

UCamera::UCamera()
 : Position(glm::vec3(0.0f, 0.0f, 0.0f))
 , WorldUp(glm::vec3(0.0f, 1.0f, 0.0f))
 , Yaw(0.0)
 , Pitch(0.0)
 , Front(glm::vec3(0.0f, 0.0f, -1.0f))
 , MovementSpeed(SPEED)
 , MouseSensitivity(SENSITIVTY)
 , Zoom(ZOOM)
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
 : Front(glm::vec3(0.0f, 0.0f, -1.0f))
 , MovementSpeed(SPEED)
 , MouseSensitivity(SENSITIVTY)
 , Zoom(ZOOM)
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
UCamera::UCamera(float posX, float posY, float posZ, float upX, float upY, float upZ, float yaw, float pitch)
 : Front(glm::vec3(0.0f, 0.0f, -1.0f))
 , MovementSpeed(SPEED)
 , MouseSensitivity(SENSITIVTY)
 , Zoom(ZOOM)
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

glm::mat4 UCamera::GetPose() const
{
 return Pose;
}

glm::mat4 UCamera::GetProjection() const
{
 return Projection;
}

glm::mat4 UCamera::GetViewMatrix() const
{
 return Pose;
}

glm::mat4 UCamera::GetMvpMatrix() const
{
 return MvpMatrix;
}

glm::vec3 UCamera::GetPosition() const
{
 return Position;
}

void UCamera::SetPosition(const glm::vec3& value)
{
 Position = value;
 UpdatePose();
}

float UCamera::GetYaw() const
{
 return Yaw;
}

float UCamera::GetPitch() const
{
 return Pitch;
}

void UCamera::SetOrientation(float yaw, float pitch)
{
 Yaw = yaw;
 Pitch = pitch;
 if (Pitch > 89.0f) {
  Pitch = 89.0f;
 }
 if (Pitch < -89.0f) {
  Pitch = -89.0f;
 }
 UpdateCameraVectors();
 UpdatePose();
}

glm::vec3 UCamera::GetFront() const
{
 return Front;
}

void UCamera::SetAspectRatio(float value)
{
 AspectRatio = value;
 UpdatePose();
}

bool UCamera::GetFreeMove() const
{
 return locomotion_.GetMode() == PlayerMovementMode::Flying;
}

void UCamera::SyncFreeMoveFromController()
{
 FreeMove = GetFreeMove();
}

void UCamera::SetFreeMove(bool value)
{
 locomotion_.SetMode(value ? PlayerMovementMode::Flying : PlayerMovementMode::Walking);
 SyncFreeMoveFromController();
 UpdatePose();
}

PlayerCapsule UCamera::GetPlayerCapsule() const
{
 return locomotion_.GetCapsule();
}

float UCamera::GetAnchoredFeetY() const
{
 return locomotion_.GetFeetY();
}

bool UCamera::HasAnchoredFeet() const
{
 return locomotion_.IsFeetAnchored();
}

float UCamera::GetStanceBlend() const
{
 return locomotion_.GetStanceBlend();
}

bool UCamera::IsCrouching() const
{
 return locomotion_.GetStanceBlend() > 0.5f;
}

bool UCamera::IsOnGround() const
{
 return locomotion_.IsOnGround();
}

bool UCamera::IsShiftDown() const
{
 const auto pressed = [](const std::map<size_t, bool>& keys, size_t code) {
  const auto it = keys.find(code);
  return it != keys.end() && it->second;
 };
 return pressed(KeysStatus, GLFW_KEY_LEFT_SHIFT) || pressed(KeysStatus, GLFW_KEY_RIGHT_SHIFT);
}

void UCamera::ClearShiftKeyState()
{
 KeysStatus[GLFW_KEY_LEFT_SHIFT] = false;
 KeysStatus[GLFW_KEY_RIGHT_SHIFT] = false;
}

bool UCamera::OnSpacePressed()
{
 const bool toggled = locomotion_.OnSpacePressed();
 SyncFreeMoveFromController();
 return toggled;
}

bool UCamera::TryToggleFlightOnDoubleSpace()
{
 return OnSpacePressed();
}

PlayerInput UCamera::BuildPlayerInput(bool spaceJustPressed) const
{
 PlayerInput input;
 const auto keyDown = [this](size_t key) {
  const auto it = KeysStatus.find(key);
  return it != KeysStatus.end() && it->second;
 };
 input.moveForward = keyDown(GLFW_KEY_W);
 input.moveBack = keyDown(GLFW_KEY_S);
 input.moveLeft = keyDown(GLFW_KEY_A);
 input.moveRight = keyDown(GLFW_KEY_D);
 input.jumpHeld = keyDown(GLFW_KEY_SPACE);
 input.jumpPressed = spaceJustPressed;
 input.crouchHeld = IsShiftDown();
 return input;
}

void UCamera::SetViewEngine(UViewEngine* view_engine)
{
 ViewEngineInstance = view_engine;
}

glm::vec3 UCamera::ComputeHorizontalShift(float deltaTime)
{
 const float velocity = locomotion_.GetWalkSpeed() * deltaTime;
 glm::vec3 shift(0.0f);
 if (KeysStatus[GLFW_KEY_W]) {
  shift += glm::vec3(std::cos(radians(Yaw)), 0.0f, std::sin(radians(Yaw))) * velocity;
 }
 if (KeysStatus[GLFW_KEY_S]) {
  shift -= glm::vec3(std::cos(radians(Yaw)), 0.0f, std::sin(radians(Yaw))) * velocity;
 }
 if (KeysStatus[GLFW_KEY_A]) {
  shift -= Right * velocity;
 }
 if (KeysStatus[GLFW_KEY_D]) {
  shift += Right * velocity;
 }
 return shift;
}

void UCamera::UpdateMoveIntentFromKeys()
{
 glm::vec3 shift = ComputeHorizontalShift(1.0f);
 shift.y = 0.0f;
 if (glm::dot(shift, shift) < 1e-10f) {
  return;
 }
 lastMoveIntentDir_ = shift / std::sqrt(glm::dot(shift, shift));
 lastMoveIntentValid_ = true;
 lastMoveIntentTime_ = std::chrono::steady_clock::now();
}

glm::vec3 UCamera::GetMoveIntentDir() const
{
 auto keyDown = [this](size_t key) {
  const auto it = KeysStatus.find(key);
  return it != KeysStatus.end() && it->second;
 };
 glm::vec3 shift = glm::vec3(0.0f);
 if (keyDown(GLFW_KEY_W)) {
  shift += glm::vec3(std::cos(radians(Yaw)), 0.0f, std::sin(radians(Yaw)));
 }
 if (keyDown(GLFW_KEY_S)) {
  shift -= glm::vec3(std::cos(radians(Yaw)), 0.0f, std::sin(radians(Yaw)));
 }
 if (keyDown(GLFW_KEY_A)) {
  shift -= Right;
 }
 if (keyDown(GLFW_KEY_D)) {
  shift += Right;
 }
 shift.y = 0.0f;
 if (glm::dot(shift, shift) > 1e-10f) {
  return shift / std::sqrt(glm::dot(shift, shift));
 }
 if (lastMoveIntentValid_) {
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - lastMoveIntentTime_).count();
  if (ms >= 0 && ms < static_cast<long long>(kStepUpIntentRetainSec * 1000.0f)) {
   return lastMoveIntentDir_;
  }
 }
 return glm::vec3(0.0f);
}

bool UCamera::TickStepUpAnimation(const UWorld* world, float dt)
{
 if (!stepUpAnim_.active) {
  return false;
 }

 stepUpAnim_.elapsed += dt;
 float t = stepUpAnim_.elapsed / kStepUpAnimDuration;
 if (t >= 1.0f) {
  Position = stepUpAnim_.targetPos;
  stepUpAnim_.active = false;
  locomotion_.SyncAfterStepLanding(Position, world);
  UpdatePose();
  return true;
 }

 t = std::min(1.0f, t);
 const float ease = 1.0f - (1.0f - t) * (1.0f - t);
 const float arc = std::sin(t * 3.14159265f) * 0.14f;
 glm::vec3 desired = stepUpAnim_.startPos + (stepUpAnim_.targetPos - stepUpAnim_.startPos) * ease;
 desired.y += arc;
 Position = desired;
 UpdatePose();
 return true;
}

bool UCamera::ApplyHorizontalMovement(const UWorld* world, float deltaTime)
{
 if (TickStepUpAnimation(world, deltaTime)) {
  return true;
 }

 UpdateMoveIntentFromKeys();

 glm::vec3 shift = ComputeHorizontalShift(deltaTime);
 const bool hasShift = glm::dot(shift, shift) > 1e-10f;

 const PlayerCapsule cap = GetPlayerCapsule();
 const UWorld::SampledFluidState fluid = world->SampleFluidPhysics(Position, cap);
 if (hasShift && fluid.inFluid) {
  const float drag = 1.0f - std::min(0.95f, fluid.dragHorizontal * deltaTime * 8.0f);
  shift.x *= drag;
  shift.z *= drag;
 }

 glm::vec3 newPos = Position;
 if (hasShift) {
  newPos = world->ResolveMovement(Position, shift, cap, world->GetMovementCollisionSkipId());
 }

 const bool grounded =
     world->HasGroundSupport(Position, cap) || locomotion_.IsOnGround();
 const glm::vec3 intent = GetMoveIntentDir();
 const PlayerInput stepInput = BuildPlayerInput(false);

 bool stepped = false;
 if (world->IsStepUpEnabled() && !fluid.inFluid && grounded
     && locomotion_.IsOnGround() && locomotion_.IsFeetAnchored()
     && !stepInput.jumpHeld
     && locomotion_.GetVerticalVelocity() <= 0.05f && glm::dot(intent, intent) > 1e-10f) {
  const UWorld::StepUpProbe probe = world->ProbeStepUp(
      newPos, intent, cap, kStepUpTriggerDistance);
  if (probe.valid) {
   glm::vec3 landing = newPos;
   if (!world->GetStepUpLanding(newPos, intent, cap, kStepUpTriggerDistance,
                               landing)) {
    landing = probe.targetPos
              - glm::vec3(probe.moveDir.x * 0.18f, 0.0f, probe.moveDir.z * 0.18f);
   }
   stepUpAnim_.active = true;
   stepUpAnim_.startPos = newPos;
   stepUpAnim_.targetPos = landing;
   stepUpAnim_.elapsed = 0.0f;
   stepped = true;
  }
 }

 if (!stepped) {
  Position = newPos;
 } else {
  TickStepUpAnimation(world, deltaTime);
 }
 UpdatePose();
 return hasShift || stepped;
}

// Processes input received from any keyboard-like input system. Accepts input parameter in the form of camera defined ENUM (to abstract it from windowing systems)
void UCamera::ProcessKeyboard(const UWorld* world, Camera_Movement direction, float deltaTime,
                             const PlayerCapsule& collisionCap)
{
 const float speed = FreeMove ? locomotion_.GetFlySpeed() : locomotion_.GetWalkSpeed();
 const float velocity = speed * deltaTime;
 glm::vec3 shift(0.0f);

 if (direction == FORWARD) {
  shift += FreeMove ? Front * velocity
                    : glm::vec3(std::cos(radians(Yaw)), 0.0f, std::sin(radians(Yaw))) * velocity;
 } else if (direction == BACKWARD) {
  shift -= FreeMove ? Front * velocity
                    : glm::vec3(std::cos(radians(Yaw)), 0.0f, std::sin(radians(Yaw))) * velocity;
 } else if (direction == LEFT) {
  shift -= Right * velocity;
 } else if (direction == RIGHT) {
  shift += Right * velocity;
 } else if (direction == UP) {
  shift += Up * velocity;
 } else if (direction == DOWN) {
  shift -= Up * velocity;
 }

 if (glm::dot(shift, shift) < 1e-10f) {
  return;
 }

 if (world) {
  glm::vec3 newPos = world->ResolveMovement(Position, shift, collisionCap,
                                            world->GetMovementCollisionSkipId());
  Position = newPos;
 } else {
  Position += shift;
 }
 UpdatePose();
}

// Processes input received from a mouse input system. Expects the offset value in both the x and y direction.
void UCamera::ProcessMouseMovement(float xoffset, float yoffset, bool constrainPitch)
{
 xoffset *= this->MouseSensitivity;
 yoffset *= this->MouseSensitivity;

 this->Yaw   += xoffset;
 this->Pitch += yoffset;

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

// Processes input received from a mouse scroll-wheel event. Only requires input on the vertical wheel-axis
void UCamera::ProcessMouseScroll(float yoffset)
{
 if (this->Zoom >= 1.0f && this->Zoom <= 45.0f)
     this->Zoom -= yoffset;
 if (this->Zoom <= 1.0f)
     this->Zoom = 1.0f;
 if (this->Zoom >= 45.0f)
     this->Zoom = 45.0f;
 UpdatePose();
}

 void UCamera::UpdateCameraVectors()
 {
  // Calculate the new Front vector
  glm::vec3 front;
  front.x = std::cos(radians(this->Yaw)) * std::cos(radians(this->Pitch));
  front.y = std::sin(radians(this->Pitch));
  front.z = std::sin(radians(this->Yaw)) * std::cos(radians(this->Pitch));
  const float frontLen = glm::length(front);
  if (frontLen > 1.0e-6f) {
   front /= frontLen;
  } else {
   front = glm::vec3(0.0f, 0.0f, -1.0f);
  }
  this->Front = front;

  // Avoid gimbal lock when looking near straight up/down (cross(Front, Up) -> NaN).
  glm::vec3 worldUp = WorldUp;
  if (std::abs(glm::dot(Front, worldUp)) > 0.98f) {
   worldUp = glm::vec3(0.0f, 0.0f, Front.y > 0.0f ? -1.0f : 1.0f);
  }

  this->Right = glm::cross(Front, worldUp);
  const float rightLen = glm::length(Right);
  if (rightLen > 1.0e-6f) {
   this->Right /= rightLen;
  } else {
   this->Right = glm::vec3(1.0f, 0.0f, 0.0f);
  }

  this->Up = glm::cross(Right, Front);
  const float upLen = glm::length(Up);
  if (upLen > 1.0e-6f) {
   this->Up /= upLen;
  } else {
   this->Up = WorldUp;
  }

  UpdatePose();
 }

glm::vec3 UCamera::ComputeCameraWorldPosition() const
{
 if (perspective_ == CameraPerspective::FirstPerson) {
  return Position;
 }
 if (perspective_ == CameraPerspective::ThirdPersonBack) {
  return Position - Front * thirdPersonDistance_ + Up * thirdPersonHeight_;
 }
 return Position + Front * thirdPersonDistance_;
}

void UCamera::CyclePerspective()
{
 perspective_ = CycleCameraPerspective(perspective_);
 UpdatePose();
}

void UCamera::UpdatePose()
{
 const glm::vec3 eye = Position;
 const glm::vec3 camWorld = ComputeCameraWorldPosition();
 const glm::vec3 target = eye + Front;
 Pose = glm::lookAt(camWorld, target, Up);

 Projection = glm::perspective(glm::radians(Fov), AspectRatio, NearPlane, FarPlane);

 MvpMatrix = Projection * Pose;
}

void UCamera::UpdateKeyStatus(size_t key_index, bool is_pressed)
{
 KeysStatus[key_index] = is_pressed;
}

void UCamera::ResetAllKeyStatus()
{
 for(auto I=KeysStatus.begin(); I!=KeysStatus.end();++I)
  I->second = false;
}

void UCamera::UpdateMouseMove(std::shared_ptr<UWorld> world, double xpos, double ypos)
{
 if(FirstMouseCoords)
 {
     LastMouseX = xpos;
     LastMouseY = ypos;
     FirstMouseCoords = false;
 }

 const double xoffset = xpos - LastMouseX;
 const double yoffset = LastMouseY - ypos;  // Reversed since y-coordinates go from bottom to left

 LastMouseX = xpos;
 LastMouseY = ypos;

 ProcessMouseMovement(static_cast<float>(xoffset), static_cast<float>(yoffset));
 world->UpdateIntersection(GetPosition(), GetFront());
}

void UCamera::ResetMouseMove(double xpos, double ypos)
{
 LastMouseX = xpos;
 LastMouseY = ypos;
 FirstMouseCoords = true;
}

void UCamera::UpdateMouseScroll(double xoffset, double yoffset)
{
 ProcessMouseScroll(static_cast<float>(yoffset));
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
 locomotion_.SetCollisionProfile(
     glm::vec3(stand.halfWidth * 2.0f, stand.height, stand.halfWidth * 2.0f), stand.eyeHeight);
}

void UCamera::ApplyCreatureLocomotion(const CreatureLocomotionCapabilities& caps,
                                     const CreatureBoundsProfile& bounds, float eyeHeight)
{
 locomotion_.SetCapabilities(caps);
 const glm::vec3 size = bounds.restSizeBlocks.x > 0.0f ? bounds.restSizeBlocks
                                                       : glm::vec3(0.6f, 1.8f, 0.6f);
 locomotion_.SetCollisionProfile(size, eyeHeight);
 MovementSpeed = caps.walkSpeed;
}

void UCamera::ResetVerticalPhysics()
{
 locomotion_.Reset();
 InitLocomotionCollisionProfile();
 stepUpAnim_.active = false;
 SyncFreeMoveFromController();
 UpdatePose();
}

bool UCamera::DoMovement(const UWorld* world)
{
 const float dt = std::min(static_cast<float>(DeltaTime), kMaxPhysicsDelta);
 const PlayerCapsule flightCap = PlayerCapsule::Standing();
 const PlayerInput input = BuildPlayerInput(false);

 bool is_moved(false);
 SyncFreeMoveFromController();

 if (GetFreeMove()) {
  const bool groundedInFlight =
      world && world->HasGroundSupport(Position, flightCap);
  if (groundedInFlight) {
   if (IsShiftDown()) {
    ClearShiftKeyState();
   }
   locomotion_.OnLandedFromFlight(world, Position, false);
   SyncFreeMoveFromController();
  } else {
   if (KeysStatus[GLFW_KEY_W]) {
    ProcessKeyboard(world, FORWARD, dt, flightCap);
    is_moved = true;
   }
   if (KeysStatus[GLFW_KEY_S]) {
    ProcessKeyboard(world, BACKWARD, dt, flightCap);
    is_moved = true;
   }
   if (KeysStatus[GLFW_KEY_A]) {
    ProcessKeyboard(world, LEFT, dt, flightCap);
    is_moved = true;
   }
   if (KeysStatus[GLFW_KEY_D]) {
    ProcessKeyboard(world, RIGHT, dt, flightCap);
    is_moved = true;
   }
   if (KeysStatus[GLFW_KEY_SPACE]) {
    ProcessKeyboard(world, UP, dt, flightCap);
    is_moved = true;
   }
   if (IsShiftDown() && world && !world->HasGroundSupport(Position, flightCap)) {
    ProcessKeyboard(world, DOWN, dt, flightCap);
    is_moved = true;
   }
   if (!input.jumpHeld) {
    locomotion_.NotifySpaceReleased();
   }
  }
 } else if (world) {
  if (ApplyHorizontalMovement(world, dt)) {
   is_moved = true;
  }
 }

 if (!GetFreeMove() && world) {
  if (IsStepUpAnimationActive()) {
   is_moved = true;
   UpdatePose();
   return is_moved;
  }
  if (Position.y < kMinReasonablePlayerY) {
   return is_moved;
  }

  locomotion_.UpdateLocomotion(world, Position, input, dt, world->GetMovementCollisionSkipId());
  if (locomotion_.ConsumeClearShiftRequest()) {
   ClearShiftKeyState();
  }
  is_moved = true;
  UpdatePose();
 }

 return is_moved;
}


}

