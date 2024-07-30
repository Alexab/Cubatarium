#ifndef CAMERA_H
#define CAMERA_H

#include <vector>

#include <QMatrix4x4>
#include <QQuaternion>
#include <QVector2D>

namespace cutum {

class ViewEngine;
class World;

// Default camera values
const float YAW        = -90.0f;
const float PITCH      =  0.0f;
const float SPEED      =  3.0f;
const float SENSITIVTY =  0.25f;
const float ZOOM       =  45.0f;


// Defines several possible options for camera movement. Used as abstraction to stay away from window-system specific input methods
enum Camera_Movement {
    FORWARD,
    BACKWARD,
    LEFT,
    RIGHT,
    UP,
    DOWN
};

class Camera
{
public:
 Camera();
 Camera(const Camera &) = default;
 // Constructor with vectors
 Camera(QVector3D position, QVector3D up = QVector3D(0.0f, 1.0f, 0.0f), float yaw = YAW, float pitch = PITCH);
 Camera(float posX, float posY, float posZ, float upX, float upY, float upZ, float yaw, float pitch);

 QVector3D GetPosition() const;
 QVector3D GetFront() const;

 QMatrix4x4 GetPose() const;
 QMatrix4x4 GetProjection() const;
 QMatrix4x4 GetViewMatrix() const;
 QMatrix4x4 GetMvpMatrix() const;

 bool GetFreeMove() const;
 void SetFreeMove(bool value);

 void SetViewEngine(ViewEngine* view_engine);

public:
 void SetAspectRatio(float value);

public:
 bool DoMovement(const World* world);

 void UpdateKeyStatus(size_t key_index, bool is_pressed);
 void ResetAllKeyStatus();
 void UpdateFrameTime();
 void UpdateMouseMove(std::shared_ptr<World> world, double xpos, double ypos);
 void ResetMouseMove(double xpos, double ypos);
 void UpdateMouseScroll(double xoffset, double yoffset);

private:
 // Processes input received from any keyboard-like input system. Accepts input parameter in the form of camera defined ENUM (to abstract it from windowing systems)
 void ProcessKeyboard(const World* world, Camera_Movement direction, float deltaTime);

 // Processes input received from a mouse input system. Expects the offset value in both the x and y direction.
 void ProcessMouseMovement(float xoffset, float yoffset, bool constrainPitch = true);

 // Processes input received from a mouse scroll-wheel event. Only requires input on the vertical wheel-axis
 void ProcessMouseScroll(float yoffset);

private:
 void UpdatePose();
 void UpdateCameraVectors();

private:
 float Fov;
 float AspectRatio;
 float NearPlane;
 float FarPlane;

private:
 // Camera Attributes
 QVector3D Position;
 QVector3D Front;
 QVector3D Up;
 QVector3D Right;
 QVector3D WorldUp;

 bool FreeMove;

 // Eular Angles
 float Yaw;
 float Pitch;

 // Camera options
 float MovementSpeed;
 float MouseSensitivity;
 float Zoom;

 QMatrix4x4 Pose;
 QMatrix4x4 Projection;
 QMatrix4x4 MvpMatrix;

 ViewEngine* ViewEngineInstance;

 float ViewObjectSize;


 std::map<size_t, bool> KeysStatus;
 float DeltaTime;
 std::chrono::time_point<std::chrono::steady_clock> LastFrame;

 float LastMouseX;
 float LastMouseY;
 bool FirstMouseCoords;
};

}
#endif // CAMERA_H
