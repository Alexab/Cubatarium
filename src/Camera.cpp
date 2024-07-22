#include "Camera.h"
#include "ViewEngine.h"
#include "World.h"

namespace cutum {

Camera::Camera()
 : Position(QVector3D(0.0f, 0.0f, 0.0f))
 , WorldUp(QVector3D(0.0f, -1.0f, 0.0f))
 , Yaw(0.0)
 , Pitch(0.0)
 , Front(QVector3D(0.0f, 0.0f, -1.0f))
 , MovementSpeed(SPEED)
 , MouseSensitivity(SENSITIVTY)
 , Zoom(ZOOM)
{
 ViewEngineInstance = nullptr;
 // Calculate aspect ratio
 AspectRatio = 1.3f;

 // Set near plane to 3.0, far plane to 7.0, field of view 45 degrees
 NearPlane = 0.1f;
 FarPlane = 70.0f;
 Fov = 90.0f;

 FreeMove = false;

 ViewObjectSize = 0.9f;

 Projection.setToIdentity();

 DeltaTime = 0.0f;
 LastFrame = std::chrono::steady_clock::now();
 LastMouseX = 0;
 LastMouseY = 0;
 FirstMouseCoords = true;

 UpdateCameraVectors();
}

// Constructor with vectors
Camera::Camera(QVector3D position, QVector3D up, float yaw, float pitch)
 : Front(QVector3D(0.0f, 0.0f, -1.0f))
 , MovementSpeed(SPEED)
 , MouseSensitivity(SENSITIVTY)
 , Zoom(ZOOM)
{
 ViewEngineInstance = nullptr;

 // Calculate aspect ratio
 AspectRatio = 1.3f;

 // Set near plane to 3.0, far plane to 7.0, field of view 45 degrees
 NearPlane = 0.1f;
 FarPlane = 70.0f;
 Fov = 90.0f;
 Projection.setToIdentity();

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
 : Front(QVector3D(0.0f, 0.0f, -1.0f))
 , MovementSpeed(SPEED)
 , MouseSensitivity(SENSITIVTY)
 , Zoom(ZOOM)
{
 ViewEngineInstance = nullptr;

 // Calculate aspect ratio
 AspectRatio = 1.3f;

 // Set near plane to 3.0, far plane to 7.0, field of view 45 degrees
 NearPlane = 0.1f;
 FarPlane = 70.0f;
 Fov = 90.0f;
 Projection.setToIdentity();

 FreeMove = false;

 ViewObjectSize = 0.9f;

 this->Position = QVector3D(posX, posY, posZ);
 this->WorldUp = QVector3D(upX, upY, upZ);
 this->Yaw = yaw;
 this->Pitch = pitch;

 DeltaTime = 0.0f;
 LastFrame = std::chrono::steady_clock::now();
 LastMouseX = 0;
 LastMouseY = 0;
 FirstMouseCoords = true;

 UpdateCameraVectors();
}

QMatrix4x4 Camera::GetPose() const
{
 return Pose;
}

QMatrix4x4 Camera::GetProjection() const
{
 return Projection;
}

QMatrix4x4 Camera::GetViewMatrix() const
{
 return Pose;
}

QMatrix4x4 Camera::GetMvpMatrix() const
{
 return MvpMatrix;
}

QVector3D Camera::GetPosition() const
{
 return Position;
}

QVector3D Camera::GetFront() const
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
 UpdatePose();
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
  QVector3D position = Position;
  QVector3D summary_shift(0.0f, 0.0f, 0.0f);

  if (direction == FORWARD)
  {
   auto shift = QVector3D(glm::cos(glm::radians(Yaw)), 0, glm::sin(glm::radians(Yaw))) * velocity;
   if(!world->CheckCollision(position+shift, ViewObjectSize))
   {
    position += shift;
    summary_shift += shift;
   }
  }
  if (direction == BACKWARD)
  {
   auto shift = QVector3D(glm::cos(glm::radians(Yaw)), 0, glm::sin(glm::radians(Yaw))) * velocity; //Y is not affected, Y is looking up
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
  QVector3D front;
  front.setX(cos(glm::radians(this->Yaw)) * cos(glm::radians(this->Pitch)));
  front.setY(sin(glm::radians(this->Pitch)));
  front.setZ(sin(glm::radians(this->Yaw)) * cos(glm::radians(this->Pitch)));
  front.normalize();
  this->Front = front;

  // Also re-calculate the Right and Up vector
  this->Right=QVector3D::crossProduct(Front, WorldUp).normalized();

  this->Up = QVector3D::crossProduct(this->Right, this->Front).normalized();

  UpdatePose();
 }

void Camera::UpdatePose()
{
 QMatrix4x4 pose;
 pose.lookAt(this->Position, this->Position + this->Front, this->Up);

 Pose = pose;

 Projection.setToIdentity();
 Projection.perspective(Fov, AspectRatio, NearPlane, FarPlane);

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

bool Camera::DoMovement(const World* world)
{
 // Camera controls
 bool is_moved(false);
 if(KeysStatus[Qt::Key_W])
 {
  ProcessKeyboard(world, FORWARD, DeltaTime);
  is_moved = true;
 }
 if(KeysStatus[Qt::Key_S])
 {
  ProcessKeyboard(world, BACKWARD, DeltaTime);
  is_moved = true;
 }
 if(KeysStatus[Qt::Key_A])
 {
  ProcessKeyboard(world, LEFT, DeltaTime);
  is_moved = true;
 }
 if(KeysStatus[Qt::Key_D])
 {
  ProcessKeyboard(world, RIGHT, DeltaTime);
  is_moved = true;
 }
 if(KeysStatus[Qt::Key_Q])
 {
  ProcessKeyboard(world, UP, DeltaTime);
  is_moved = true;
 }
 if(KeysStatus[Qt::Key_E])
 {
  ProcessKeyboard(world, DOWN, DeltaTime);
  is_moved = true;
 }

 return is_moved;
}


}

