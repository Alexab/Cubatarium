#include <cmath>
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


// Processes input received from any keyboard-like input system. Accepts input parameter in the form of camera defined ENUM (to abstract it from windowing systems)
void Camera::ProcessKeyboard(const World* world, Camera_Movement direction, float deltaTime)
{
 float velocity = this->MovementSpeed * deltaTime;

 if(FreeMove)
 {
  if (direction == FORWARD)
   this->Position += this->Front * velocity;
  if (direction == BACKWARD)
   this->Position -= this->Front * velocity;
  if (direction == LEFT)
   this->Position -= this->Right * velocity;
  if (direction == RIGHT)
   this->Position += this->Right * velocity;
  if (direction == UP)
   this->Position += this->Up * velocity;
  if (direction == DOWN)
   this->Position -= this->Up * velocity;
 }
 else
 {
  glm::vec3 position = Position;
  glm::vec3 summary_shift(0.0f, 0.0f, 0.0f);

  if (direction == FORWARD)
  {
   auto shift = glm::vec3(std::cos(radians(Yaw)), 0, std::sin(radians(Yaw))) * velocity;
   if(!world->CheckCollision(position+shift, ViewObjectSize))
   {
    position += shift;
    summary_shift += shift;
   }
  }
  if (direction == BACKWARD)
  {
   auto shift = glm::vec3(std::cos(radians(Yaw)), 0, std::sin(radians(Yaw))) * velocity; //Y is not affected, Y is looking up
   if(!world->CheckCollision(position-shift, ViewObjectSize))
   {
    position -= shift;
    summary_shift -= shift;
   }
  }
  if (direction == LEFT)
  {
   auto shift = this->Right * velocity;
   if(!world->CheckCollision(position-shift, ViewObjectSize))
   {
    position -= shift;
    summary_shift -= shift;
   }
  }
  if (direction == RIGHT)
  {
   auto shift = this->Right * velocity;
   if(!world->CheckCollision(position+shift, ViewObjectSize))
   {
    position += shift;
    summary_shift += shift;
   }
  }
  if (direction == UP)
  {
   auto shift = this->Up * velocity;
   if(!world->CheckCollision(position+shift, ViewObjectSize))
   {
    position += shift;
    summary_shift += shift;
   }
  }
  if (direction == DOWN)
  {
   auto shift = this->Up * velocity;
   if(!world->CheckCollision(position-shift, ViewObjectSize))
   {
    position -= shift;
    summary_shift -= shift;
   }
  }

  if(!world->CheckCollision(Position, ViewObjectSize))
   Position = position;
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
  front = glm::normalize(front);
  this->Front = front;

  // Also re-calculate the Right and Up vector
  this->Right=glm::cross(Front, WorldUp);
  this->Right = glm::normalize(this->Right);

  this->Up = glm::cross(this->Right, this->Front);
  this->Up = glm::normalize(this->Up);

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
 if(FirstMouseCoords)
 {
     LastMouseX = xpos;
     LastMouseY = ypos;
     FirstMouseCoords = false;
 }

// float xoffset = xpos - LastMouseX;
// float yoffset = LastMouseY - ypos;  // Reversed since y-coordinates go from bottom to left

 LastMouseX = xpos;
 LastMouseY = ypos;
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
 spaceWasPressed_ = KeysStatus[GLFW_KEY_SPACE];
 UpdatePose();
}

bool Camera::DoMovement(const World* world)
{
 // Camera controls
 bool is_moved(false);
 if(KeysStatus[GLFW_KEY_W])
 {
  ProcessKeyboard(world, FORWARD, DeltaTime);
  is_moved = true;
 }
 if(KeysStatus[GLFW_KEY_S])
 {
  ProcessKeyboard(world, BACKWARD, DeltaTime);
  is_moved = true;
 }
 if(KeysStatus[GLFW_KEY_A])
 {
  ProcessKeyboard(world, LEFT, DeltaTime);
  is_moved = true;
 }
 if(KeysStatus[GLFW_KEY_D])
 {
  ProcessKeyboard(world, RIGHT, DeltaTime);
  is_moved = true;
 }
 if (KeysStatus[GLFW_KEY_Q] || (FreeMove && KeysStatus[GLFW_KEY_SPACE])) {
  ProcessKeyboard(world, UP, DeltaTime);
  is_moved = true;
 }
 if(KeysStatus[GLFW_KEY_E])
 {
  ProcessKeyboard(world, DOWN, DeltaTime);
  is_moved = true;
 }

 if (!FreeMove && world)
 {
  if (Position.y < kMinReasonablePlayerY) {
   verticalVelocity_ = 0.0f;
   onGround_ = false;
   return is_moved;
  }
  verticalVelocity_ += kGravity * static_cast<float>(DeltaTime);
  glm::vec3 pos = Position;
  pos.y += verticalVelocity_ * static_cast<float>(DeltaTime);
  if (world->CheckCollision(pos, ViewObjectSize)) {
   if (verticalVelocity_ < 0.0f) {
    onGround_ = true;
   }
   verticalVelocity_ = 0.0f;
   while (world->CheckCollision(Position, ViewObjectSize)) {
    Position.y += 0.05f;
   }
  } else {
   onGround_ = false;
   Position.y = pos.y;
   is_moved = true;
  }

  const bool spacePressed = KeysStatus[GLFW_KEY_SPACE];
  if (!spacePressed) {
   suppressNextJump_ = false;
  }
  if (onGround_ && spacePressed && !spaceWasPressed_ && !suppressNextJump_) {
   verticalVelocity_ = kJumpSpeed;
   onGround_ = false;
   is_moved = true;
  }
  spaceWasPressed_ = spacePressed;
  UpdatePose();
 }

 return is_moved;
}


}

