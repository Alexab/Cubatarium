#ifndef CAMERA_H
#define CAMERA_H

#include <chrono>
#include <map>
#include <memory>
#include <vector>

#include "Creatures/Core/CreatureBounds.h"
#include "Creatures/Locomotion/LocomotionTypes.h"
#include "Creatures/Player/PlayerCapsule.h"
#include "Creatures/Player/PlayerController.h"
#include "Render/Camera/CameraPerspective.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace cutum
{

class UViewEngine;
class UWorld;

const float YAW = -90.0f;
const float PITCH = 0.0f;
const float SPEED = 3.0f;
const float SENSITIVTY = 0.25f;
const float ZOOM = 45.0f;

enum Camera_Movement
{
  FORWARD,
  BACKWARD,
  LEFT,
  RIGHT,
  UP,
  DOWN
};

class UCamera
{
public:
  UCamera();
  UCamera(const UCamera &) = default;
  UCamera(glm::vec3 position, glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f),
          float yaw = YAW, float pitch = PITCH);
  UCamera(float posX, float posY, float posZ, float upX, float upY, float upZ,
          float yaw, float pitch);

  /// Eye world position (not Render camera position in 3rd person).
  glm::vec3 GetPosition() const;
  void SetPosition(const glm::vec3 &value);
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

  void SetViewEngine(UViewEngine *view_engine);
  void SetAspectRatio(float value);

  bool DoMovement(const UWorld *world);
  void ResetVerticalPhysics();
  void ApplyCreatureLocomotion(const CreatureLocomotionCapabilities &caps,
                               const CreatureBoundsProfile &bounds,
                               float eyeHeight);

  PlayerCapsule GetPlayerCapsule() const;
  float GetAnchoredFeetY() const;
  bool HasAnchoredFeet() const;
  float GetStanceBlend() const;
  bool IsCrouching() const;
  float GetDeltaTime() const { return DeltaTime; }
  bool IsOnGround() const;
  const UCreatureLocomotionController &GetLocomotionController() const
  {
    return Locomotion;
  }
  bool IsStepUpAnimationActive() const { return StepUpAnim.Active; }

  CameraPerspective GetPerspective() const { return Perspective; }
  void CyclePerspective();

  void UpdateKeyStatus(size_t key_index, bool is_pressed);
  void ResetAllKeyStatus();
  void UpdateFrameTime();
  void UpdateMouseMove(std::shared_ptr<UWorld> world, double xpos, double ypos);
  void ResetMouseMove(double xpos, double ypos);
  void ApplyRelativeMouseMove(float Xoffset, float Yoffset);
  void UpdateMouseScroll(double Xoffset, double Yoffset);

  void ClearShiftKeyState();
  void SetSprintActive(bool active) { SprintActive = active; }
  PlayerInput GetMovementInput() const;

private:
  glm::vec3 ComputeHorizontalShift(float deltaTime);
  void UpdateMoveIntentFromKeys();
  glm::vec3 GetMoveIntentDir() const;
  bool ApplyHorizontalMovement(const UWorld *world, float deltaTime);
  bool TickStepUpAnimation(const UWorld *world, float dt);
  void ProcessKeyboard(const UWorld *world, Camera_Movement direction,
                       float deltaTime, const PlayerCapsule &collisionCap);
  PlayerInput BuildPlayerInput(bool spaceJustPressed) const;
  bool IsShiftDown() const;

  void ProcessMouseMovement(float Xoffset, float Yoffset,
                            bool constrainPitch = true);
  void ProcessMouseScroll(float Yoffset);
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

  UViewEngine *ViewEngineInstance;
  PlayerController Locomotion;

  std::map<size_t, bool> KeysStatus;
  float DeltaTime;
  std::chrono::time_point<std::chrono::steady_clock> LastFrame;

  double LastMouseX{0.0};
  double LastMouseY{0.0};
  bool FirstMouseCoords;

  glm::vec3 LastMoveIntentDir{0.0f, 0.0f, -1.0f};
  bool LastMoveIntentValid{false};
  std::chrono::steady_clock::time_point LastMoveIntentTime{};
  static constexpr float kStepUpTriggerDistance = 0.36f;
  static constexpr float kStepUpIntentRetainSec = 0.3f;
  static constexpr float kStepUpAnimDuration = 0.14f;
  struct StepUpAnimation
  {
    bool Active{false};
    glm::vec3 StartPos{0.0f};
    glm::vec3 TargetPos{0.0f};
    float Elapsed{0.0f};
  };
  StepUpAnimation StepUpAnim;

  bool SprintActive{false};

  CameraPerspective Perspective{CameraPerspective::FirstPerson};
  float ThirdPersonDistance{4.0f};
  float ThirdPersonHeight{0.5f};

  static constexpr float kMinReasonablePlayerY = -32.0f;
  static constexpr float kMaxPhysicsDelta = 1.0f / 30.0f;
};

} // namespace cutum
#endif
