#ifndef CAMERA_H
#define CAMERA_H

#include <vector>
#include <map>
#include <chrono>
#include <memory>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "CreatureBounds.h"
#include "LocomotionTypes.h"
#include "PlayerCapsule.h"
#include "PlayerController.h"
#include "CameraPerspective.h"

namespace cutum {

class ViewEngine;
class World;

const float YAW        = -90.0f;
const float PITCH      =  0.0f;
const float SPEED      =  3.0f;
const float SENSITIVTY =  0.25f;
const float ZOOM       =  45.0f;

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
 Camera(glm::vec3 position, glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f), float yaw = YAW, float pitch = PITCH);
 Camera(float posX, float posY, float posZ, float upX, float upY, float upZ, float yaw, float pitch);

 /// Eye world position (not render camera position in 3rd person).
 glm::vec3 GetPosition() const;
 void SetPosition(const glm::vec3& value);
 float GetYaw() const;
 float GetPitch() const;
 void SetOrientation(float yaw, float pitch);
 glm::vec3 GetFront() const;

 glm::mat4 GetPose() const;
 glm::mat4 GetProjection() const;
 glm::mat4 GetViewMatrix() const;
 glm::mat4 GetMvpMatrix() const;

 bool GetFreeMove() const;
 void SetFreeMove(bool value);

 bool TryToggleFlightOnDoubleSpace();
 bool OnSpacePressed();

 void SetViewEngine(ViewEngine* view_engine);
 void SetAspectRatio(float value);

 bool DoMovement(const World* world);
 void ResetVerticalPhysics();
 void ApplyCreatureLocomotion(const CreatureLocomotionCapabilities& caps,
                              const CreatureBoundsProfile& bounds, float eyeHeight);

 PlayerCapsule GetPlayerCapsule() const;
 float GetAnchoredFeetY() const;
 bool HasAnchoredFeet() const;
 float GetStanceBlend() const;
 bool IsCrouching() const;
 float GetDeltaTime() const { return DeltaTime; }
 bool IsOnGround() const;
 const CreatureLocomotionController& GetLocomotionController() const { return locomotion_; }
 bool IsStepUpAnimationActive() const { return stepUpAnim_.active; }

 CameraPerspective GetPerspective() const { return perspective_; }
 void CyclePerspective();

 void UpdateKeyStatus(size_t key_index, bool is_pressed);
 void ResetAllKeyStatus();
 void UpdateFrameTime();
 void UpdateMouseMove(std::shared_ptr<World> world, double xpos, double ypos);
 void ResetMouseMove(double xpos, double ypos);
 void UpdateMouseScroll(double xoffset, double yoffset);

 void ClearShiftKeyState();

private:
 glm::vec3 ComputeHorizontalShift(float deltaTime);
 void UpdateMoveIntentFromKeys();
 glm::vec3 GetMoveIntentDir() const;
 bool ApplyHorizontalMovement(const World* world, float deltaTime);
 bool TickStepUpAnimation(const World* world, float dt);
 void ProcessKeyboard(const World* world, Camera_Movement direction, float deltaTime,
                      const PlayerCapsule& collisionCap);
 PlayerInput BuildPlayerInput(bool spaceJustPressed) const;
 bool IsShiftDown() const;

 void ProcessMouseMovement(float xoffset, float yoffset, bool constrainPitch = true);
 void ProcessMouseScroll(float yoffset);
 void UpdatePose();
 glm::vec3 ComputeCameraWorldPosition() const;
 void UpdateCameraVectors();
 void SyncFreeMoveFromController();
 void InitLocomotionCollisionProfile();

 float Fov;
 float AspectRatio;
 float NearPlane;
 float FarPlane;

 glm::vec3 Position;
 glm::vec3 Front;
 glm::vec3 Up;
 glm::vec3 Right;
 glm::vec3 WorldUp;

 bool FreeMove;

 float Yaw;
 float Pitch;
 float MovementSpeed;
 float MouseSensitivity;
 float Zoom;

 glm::mat4 Pose;
 glm::mat4 Projection;
 glm::mat4 MvpMatrix;

 ViewEngine* ViewEngineInstance;
 PlayerController locomotion_;

 std::map<size_t, bool> KeysStatus;
 float DeltaTime;
 std::chrono::time_point<std::chrono::steady_clock> LastFrame;

 float LastMouseX;
 float LastMouseY;
 bool FirstMouseCoords;

 glm::vec3 lastMoveIntentDir_{0.0f, 0.0f, -1.0f};
 bool lastMoveIntentValid_{false};
 std::chrono::steady_clock::time_point lastMoveIntentTime_{};
 static constexpr float kStepUpTriggerDistance = 0.36f;
 static constexpr float kStepUpIntentRetainSec = 0.3f;
 static constexpr float kStepUpAnimDuration = 0.14f;
 struct StepUpAnimation {
  bool active{false};
  glm::vec3 startPos{0.0f};
  glm::vec3 targetPos{0.0f};
  float elapsed{0.0f};
 };
 StepUpAnimation stepUpAnim_;

 CameraPerspective perspective_{CameraPerspective::FirstPerson};
 float thirdPersonDistance_{4.0f};
 float thirdPersonHeight_{0.5f};

 static constexpr float kMinReasonablePlayerY = -32.0f;
 static constexpr float kMaxPhysicsDelta = 1.0f / 30.0f;
};

}
#endif
