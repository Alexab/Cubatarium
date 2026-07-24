#ifndef TOUCH_INPUT_BRIDGE_H
#define TOUCH_INPUT_BRIDGE_H

#include "App/Platform/InputManager.h"
#include <array>
#include <chrono>
#include <glm/glm.hpp>

namespace cutum
{

struct TouchBlockedRegion
{
  int x{0};
  int y{0};
  int w{0};
  int h{0};
};

class UTouchInputBridge
{
public:
  static constexpr int kMaxPointers = 10;
  static constexpr int kMaxBlockedRegions = 4;

  void Reset();
  void OnTouchDown(int PointerId, float x, float y, bool gameInput = true);
  void OnTouchMove(int PointerId, float x, float y, bool allowLook = true);
  void OnTouchUp(int PointerId, float x, float y, bool cancelled = false);
  void Update();

  bool IsKeyPressed(KeyCode key) const;
  bool IsMouseButtonPressed(MouseButton Button) const;
  bool IsMouseButtonJustPressed(MouseButton Button) const;
  glm::vec2 GetMousePosition() const { return MousePosition; }
  glm::vec2 GetMouseDelta() const { return MouseDelta; }

  void SetHeldKey(KeyCode key, bool Pressed);
  glm::vec2 ConsumeMouseDelta();
  bool IsMouseButtonJustReleased(MouseButton Button) const;

  void SetJoystickVector(glm::vec2 v) { Joystick = v; }
  void SetJoystickActive(bool Active);
  bool IsJoystickActive() const { return JoystickActive; }
  bool IsMovementBlockingBreak() const;
  void AddLookDelta(float dx, float dy);
  void ResetJoystick();

  void ToggleSprint();
  void SetSprintActive(bool active);
  bool IsSprintActive() const { return SprintActive; }
  void ResetSprint() { SprintActive = false; }

  bool ConsumePendingPlaceTap(glm::vec2 &outPos);
  bool ConsumeCameraBaseline(float &outX, float &outY);
  bool ConsumeBlockInputCancel();

  void SetUiScale(float scale) { UiScale = scale; }
  void SetPlaceClickMaxSeconds(float seconds)
  {
    PlaceClickMaxSeconds = seconds;
  }
  void SetBreakHoldMinSeconds(float seconds) { BreakHoldMinSeconds = seconds; }

  void SetScreenSize(int w, int h)
  {
    ScreenWidth = w;
    ScreenHeight = h;
  }
  void SetContentInsets(int left, int top, int right, int bottom)
  {
    ContentInsetLeft = left;
    ContentInsetTop = top;
    ContentInsetRight = right;
    ContentInsetBottom = bottom;
  }
  void ClearBlockedGameRegions();
  void SetBlockedGameRegion(int index, const TouchBlockedRegion &region);
  void QueuePlaceTap(float x, float y);
  void RequestCameraBaseline(float x, float y);
  int NormalizePointerId(int PointerId) const;

private:
  struct PointerState
  {
    bool Active{false};
    bool GamePointer{false};
    bool TapCandidate{false};
    bool LookZoneTouch{false};
    bool LookDrag{false};
    glm::vec2 StartPos{0.f, 0.f};
    glm::vec2 LastLookPos{0.f, 0.f};
    std::chrono::steady_clock::time_point DownTime{};
  };

  void ApplyJoystickToKeys();
  float ContentLeft() const { return static_cast<float>(ContentInsetLeft); }
  float ContentRight() const
  {
    return static_cast<float>(ScreenWidth - ContentInsetRight);
  }
  float ContentTop() const { return static_cast<float>(ContentInsetTop); }
  float ContentBottom() const
  {
    return static_cast<float>(ScreenHeight - ContentInsetBottom);
  }
  float ContentWidth() const
  {
    return std::max(1.f, ContentRight() - ContentLeft());
  }
  float LookZoneStartX() const
  {
    return ContentLeft() + ContentWidth() * 0.58f;
  }
  float LookDragThresholdPx() const;
  float PlaceTapSlopPx() const;
  bool IsBlockedGameInput(float x, float y) const;
  bool IsInLookZone(float x) const { return x >= LookZoneStartX(); }
  void QueueCameraBaseline(float x, float y);

  glm::vec2 Joystick{0.f};
  glm::vec2 MousePosition{0.f};
  glm::vec2 MouseDelta{0.f};
  glm::vec2 PendingPlacePos{0.f};
  glm::vec2 CameraBaselinePos{0.f};
  std::array<PointerState, kMaxPointers> Pointers{};
  std::array<TouchBlockedRegion, kMaxBlockedRegions> BlockedRegions{};
  int ScreenWidth{1280};
  int ScreenHeight{720};
  int ContentInsetLeft{0};
  int ContentInsetTop{0};
  int ContentInsetRight{0};
  int ContentInsetBottom{0};
  float UiScale{1.f};
  float PlaceClickMaxSeconds{0.20f};
  float BreakHoldMinSeconds{0.50f};
  bool Keys[512]{};
  bool PrevKeys[512]{};
  bool MouseButtons[8]{};
  bool PrevMouseButtons[8]{};
  bool MouseJustPressed[8]{};
  bool MouseJustReleased[8]{};
  bool ManualKeys[512]{};
  bool JoystickActive{false};
  bool SprintActive{false};
  bool PendingPlaceTap{false};
  bool CameraBaselinePending{false};
  bool BlockInputCancelPending{false};
};

} // namespace cutum

#endif
