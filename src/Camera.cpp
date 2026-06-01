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

Camera::Camera()
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

 ViewObjectSize = 0.9f;

 Projection = glm::mat4(1.0f);

 DeltaTime = 0.0f;
 LastFrame = std::chrono::steady_clock::now();
 LastMouseX = 0;
 LastMouseY = 0;
 FirstMouseCoords = true;

 UpdateCameraVectors();
}

// Constructor with vectors
Camera::Camera(glm::vec3 position, glm::vec3 up, float yaw, float pitch)
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

 ViewObjectSize = 0.9f;

 this->Position = position;
 this->WorldUp = up;
 this->Yaw = yaw;
 this->Pitch = pitch;

 DeltaTime = 0.0f;
 LastFrame = std::chrono::steady_clock::now();
 LastMouseX = 0;
 LastMouseY = 0;
 FirstMouseCoords = true;

 UpdateCameraVectors();
}

// Constructor with scalar values
Camera::Camera(float posX, float posY, float posZ, float upX, float upY, float upZ, float yaw, float pitch)
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

 ViewObjectSize = 0.9f;

 this->Position = glm::vec3(posX, posY, posZ);
 this->WorldUp = glm::vec3(upX, upY, upZ);
 this->Yaw = yaw;
 this->Pitch = pitch;

 DeltaTime = 0.0f;
 LastFrame = std::chrono::steady_clock::now();
 LastMouseX = 0;
 LastMouseY = 0;
 FirstMouseCoords = true;

 UpdateCameraVectors();
}

glm::mat4 Camera::GetPose() const
{
 return Pose;
}

glm::mat4 Camera::GetProjection() const
{
 return Projection;
}

glm::mat4 Camera::GetViewMatrix() const
{
 return Pose;
}

glm::mat4 Camera::GetMvpMatrix() const
{
 return MvpMatrix;
}

glm::vec3 Camera::GetPosition() const
{
 return Position;
}

void Camera::SetPosition(const glm::vec3& value)
{
 Position = value;
 UpdatePose();
}

float Camera::GetYaw() const
{
 return Yaw;
}

float Camera::GetPitch() const
{
 return Pitch;
}

void Camera::SetOrientation(float yaw, float pitch)
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

glm::vec3 Camera::GetFront() const
{
 return Front;
}

void Camera::SetAspectRatio(float value)
{
 AspectRatio = value;
 UpdatePose();
}

bool Camera::GetFreeMove() const
{
 return FreeMove;
}

void Camera::SetFreeMove(bool value)
{
 FreeMove = value;
 if (!FreeMove) {
  verticalVelocity_ = 0.0f;
 }
 UpdatePose();
}

bool Camera::TryToggleFlightOnDoubleSpace()
{
 const auto now = std::chrono::steady_clock::now();
 if (lastSpacePressTime_.time_since_epoch().count() != 0) {
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      now - lastSpacePressTime_).count();
  if (ms >= 0 && ms < kDoubleSpaceTapMs) {
   SetFreeMove(!FreeMove);
   suppressNextJump_ = true;
   lastSpacePressTime_ = {};
   return true;
  }
 }
 lastSpacePressTime_ = now;
 return false;
}

void Camera::SetViewEngine(ViewEngine* view_engine)
{
 ViewEngineInstance = view_engine;
}

glm::vec3 Camera::ComputeHorizontalShift(float deltaTime)
{
 const float velocity = MovementSpeed * deltaTime;
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

void Camera::UpdateMoveIntentFromKeys()
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

glm::vec3 Camera::GetMoveIntentDir() const
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

bool Camera::TickStepUpAnimation(const World* /*world*/, float dt)
{
 if (!stepUpAnim_.active) {
  return false;
 }

 stepUpAnim_.elapsed += dt;
 float t = stepUpAnim_.elapsed / kStepUpAnimDuration;
 if (t >= 1.0f) {
  Position = stepUpAnim_.targetPos;
  stepUpAnim_.active = false;
  verticalVelocity_ = 0.0f;
  onGround_ = true;
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

bool Camera::ApplyHorizontalMovement(const World* world, float deltaTime)
{
 if (TickStepUpAnimation(world, deltaTime)) {
  return true;
 }

 UpdateMoveIntentFromKeys();

 glm::vec3 shift = ComputeHorizontalShift(deltaTime);
 const bool hasShift = glm::dot(shift, shift) > 1e-10f;

 const World::SampledFluidState fluid = world->SampleFluidPhysics(Position, ViewObjectSize);
 if (hasShift && fluid.inFluid) {
  const float drag = 1.0f - std::min(0.95f, fluid.dragHorizontal * deltaTime * 8.0f);
  shift.x *= drag;
  shift.z *= drag;
 }

 glm::vec3 newPos = Position;
 if (hasShift) {
  newPos = world->ResolveMovement(Position, shift, ViewObjectSize);
 }

 const bool grounded =
     world->HasGroundSupport(Position, ViewObjectSize) || onGround_;
 const glm::vec3 intent = GetMoveIntentDir();

 bool stepped = false;
 if (world->IsStepUpEnabled() && !fluid.inFluid && grounded
     && verticalVelocity_ <= 0.05f && glm::dot(intent, intent) > 1e-10f) {
  const World::StepUpProbe probe = world->ProbeStepUp(
      newPos, intent, ViewObjectSize, kStepUpTriggerDistance);
  if (probe.valid) {
   glm::vec3 landing = newPos;
   if (!world->GetStepUpLanding(newPos, intent, ViewObjectSize, kStepUpTriggerDistance,
                               landing)) {
    landing = probe.targetPos
              - glm::vec3(probe.moveDir.x * 0.18f, 0.0f, probe.moveDir.z * 0.18f);
   }
   stepUpAnim_.active = true;
   stepUpAnim_.startPos = newPos;
   stepUpAnim_.targetPos = landing;
   stepUpAnim_.elapsed = 0.0f;
   verticalVelocity_ = 0.0f;
   onGround_ = false;
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
void Camera::ProcessKeyboard(const World* world, Camera_Movement direction, float deltaTime)
{
 const float velocity = MovementSpeed * deltaTime;
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
  glm::vec3 newPos = world->ResolveMovement(Position, shift, ViewObjectSize);
  Position = newPos;
 } else {
  Position += shift;
 }
 UpdatePose();
}

// Processes input received from a mouse input system. Expects the offset value in both the x and y direction.
void Camera::ProcessMouseMovement(float xoffset, float yoffset, bool constrainPitch)
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
void Camera::ProcessMouseScroll(float yoffset)
{
 if (this->Zoom >= 1.0f && this->Zoom <= 45.0f)
     this->Zoom -= yoffset;
 if (this->Zoom <= 1.0f)
     this->Zoom = 1.0f;
 if (this->Zoom >= 45.0f)
     this->Zoom = 45.0f;
 UpdatePose();
}

 void Camera::UpdateCameraVectors()
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

void Camera::UpdatePose()
{
 glm::mat4 pose = glm::lookAt(this->Position, this->Position + this->Front, this->Up);

 Pose = pose;

 Projection = glm::perspective(glm::radians(Fov), AspectRatio, NearPlane, FarPlane);

 MvpMatrix = Projection * Pose;
}

void Camera::UpdateKeyStatus(size_t key_index, bool is_pressed)
{
 KeysStatus[key_index] = is_pressed;
}

void Camera::ResetAllKeyStatus()
{
 for(auto I=KeysStatus.begin(); I!=KeysStatus.end();++I)
  I->second = false;
}

void Camera::UpdateMouseMove(std::shared_ptr<World> world, double xpos, double ypos)
{
 if(FirstMouseCoords)
 {
     LastMouseX = xpos;
     LastMouseY = ypos;
     FirstMouseCoords = false;
 }

 float xoffset = xpos - LastMouseX;
 float yoffset = LastMouseY - ypos;  // Reversed since y-coordinates go from bottom to left

 LastMouseX = xpos;
 LastMouseY = ypos;

 ProcessMouseMovement(xoffset, yoffset);
 world->UpdateIntersection(GetPosition(), GetFront());
}

void Camera::ResetMouseMove(double xpos, double ypos)
{
 LastMouseX = xpos;
 LastMouseY = ypos;
 FirstMouseCoords = true;
}

void Camera::UpdateMouseScroll(double xoffset, double yoffset)
{
 ProcessMouseScroll(yoffset);
}

void Camera::UpdateFrameTime()
{
 auto current_frame = std::chrono::steady_clock::now();
 DeltaTime = std::chrono::duration_cast<std::chrono::milliseconds>(current_frame-LastFrame).count() / 1000.0;
 LastFrame = current_frame;
}

void Camera::ResetVerticalPhysics()
{
 verticalVelocity_ = 0.0f;
 onGround_ = true;
 suppressNextJump_ = false;
 stepUpAnim_.active = false;
 spaceWasPressed_ = KeysStatus[GLFW_KEY_SPACE];
 UpdatePose();
}

bool Camera::DoMovement(const World* world)
{
 const float dt = std::min(static_cast<float>(DeltaTime), kMaxPhysicsDelta);

 bool is_moved(false);
 if (FreeMove) {
  if (KeysStatus[GLFW_KEY_W]) {
   ProcessKeyboard(world, FORWARD, dt);
   is_moved = true;
  }
  if (KeysStatus[GLFW_KEY_S]) {
   ProcessKeyboard(world, BACKWARD, dt);
   is_moved = true;
  }
  if (KeysStatus[GLFW_KEY_A]) {
   ProcessKeyboard(world, LEFT, dt);
   is_moved = true;
  }
  if (KeysStatus[GLFW_KEY_D]) {
   ProcessKeyboard(world, RIGHT, dt);
   is_moved = true;
  }
  if (KeysStatus[GLFW_KEY_Q] || KeysStatus[GLFW_KEY_SPACE]) {
   ProcessKeyboard(world, UP, dt);
   is_moved = true;
  }
  if (KeysStatus[GLFW_KEY_E]) {
   ProcessKeyboard(world, DOWN, dt);
   is_moved = true;
  }
 } else if (world) {
  if (ApplyHorizontalMovement(world, dt)) {
   is_moved = true;
  }
  if (KeysStatus[GLFW_KEY_Q]) {
   ProcessKeyboard(world, UP, dt);
   is_moved = true;
  }
  if (KeysStatus[GLFW_KEY_E]) {
   ProcessKeyboard(world, DOWN, dt);
   is_moved = true;
  }
 } else {
  if (KeysStatus[GLFW_KEY_W]) {
   ProcessKeyboard(world, FORWARD, dt);
   is_moved = true;
  }
  if (KeysStatus[GLFW_KEY_S]) {
   ProcessKeyboard(world, BACKWARD, dt);
   is_moved = true;
  }
  if (KeysStatus[GLFW_KEY_A]) {
   ProcessKeyboard(world, LEFT, dt);
   is_moved = true;
  }
  if (KeysStatus[GLFW_KEY_D]) {
   ProcessKeyboard(world, RIGHT, dt);
   is_moved = true;
  }
 }

 if (!FreeMove && world)
 {
  if (IsStepUpAnimationActive()) {
   spaceWasPressed_ = KeysStatus[GLFW_KEY_SPACE];
   is_moved = true;
   UpdatePose();
   return is_moved;
  }
  if (Position.y < kMinReasonablePlayerY) {
   verticalVelocity_ = 0.0f;
   onGround_ = false;
   return is_moved;
  }
  const World::SampledFluidState fluid = world->SampleFluidPhysics(Position, ViewObjectSize);
  const bool spacePressed = KeysStatus[GLFW_KEY_SPACE];
  if (!spacePressed) {
   suppressNextJump_ = false;
  }

  if (fluid.inFluid) {
   if (spacePressed && !spaceWasPressed_) {
    verticalVelocity_ = std::max(verticalVelocity_, 6.5f);
   }
   if (spacePressed) {
    verticalVelocity_ += fluid.riseSpeed * 5.0f * dt;
   }
   verticalVelocity_ *= 1.0f - std::min(0.9f, fluid.dragHorizontal * dt * 6.0f);
   if (!spacePressed) {
    verticalVelocity_ -= fluid.sinkSpeed * dt;
   }
   verticalVelocity_ += kGravity * 0.15f * dt;
   onGround_ = false;

   const glm::vec3 verticalDelta(0.0f, verticalVelocity_ * dt, 0.0f);
   Position = world->ResolveMovement(Position, verticalDelta, ViewObjectSize);
  } else {
   verticalVelocity_ += kGravity * dt;
   const glm::vec3 verticalDelta(0.0f, verticalVelocity_ * dt, 0.0f);
   const glm::vec3 resolved = world->ResolveMovement(Position, verticalDelta, ViewObjectSize);
   const float movedY = resolved.y - Position.y;
   const float requestedY = verticalDelta.y;
   if (std::abs(movedY - requestedY) > 1e-4f) {
    if (requestedY < 0.0f) {
     onGround_ = true;
    }
    verticalVelocity_ = 0.0f;
   } else {
    onGround_ = false;
   }
   Position = resolved;

   if (onGround_ && verticalVelocity_ <= 0.05f && spacePressed && !spaceWasPressed_
       && !suppressNextJump_) {
    verticalVelocity_ = kJumpSpeed;
    onGround_ = false;
    is_moved = true;
   }
  }
  spaceWasPressed_ = spacePressed;
  is_moved = true;
  UpdatePose();
 }

 return is_moved;
}


}

