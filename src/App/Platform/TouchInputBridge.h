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

class TouchInputBridge
{
public:
  static constexpr int kMaxPointers = 10;
  static constexpr int kMaxBlockedRegions = 4;

  void Reset();
  void OnTouchDown(int pointerId, float x, float y, bool gameInput = true);
  void OnTouchMove(int pointerId, float x, float y, bool allowLook = true);
  void OnTouchUp(int pointerId, float x, float y, bool cancelled = false);
  void Update();

  bool IsKeyPressed(KeyCode key) const;
  bool IsMouseButtonPressed(MouseButton button) const;
  bool IsMouseButtonJustPressed(MouseButton button) const;
  glm::vec2 GetMousePosition() const { return mousePosition_; }
  glm::vec2 GetMouseDelta() const { return mouseDelta_; }

  void SetHeldKey(KeyCode key, bool pressed);
  glm::vec2 ConsumeMouseDelta();
  bool IsMouseButtonJustReleased(MouseButton button) const;

  void SetJoystickVector(glm::vec2 v) { joystick_ = v; }
  void SetJoystickActive(bool active);
  bool IsJoystickActive() const { return joystickActive_; }
  bool IsMovementBlockingBreak() const;
  void AddLookDelta(float dx, float dy);
  void ResetJoystick();

  bool ConsumePendingPlaceTap(glm::vec2 &outPos);
  bool ConsumeCameraBaseline(float &outX, float &outY);
  bool ConsumeBlockInputCancel();

  void SetUiScale(float scale) { uiScale_ = scale; }
  void SetPlaceClickMaxSeconds(float seconds)
  {
    placeClickMaxSeconds_ = seconds;
  }
  void SetBreakHoldMinSeconds(float seconds)
  {
    breakHoldMinSeconds_ = seconds;
  }

  void SetScreenSize(int w, int h)
  {
    screenWidth_ = w;
    screenHeight_ = h;
  }
  void SetContentInsets(int left, int top, int right, int bottom)
  {
    contentInsetLeft_ = left;
    contentInsetTop_ = top;
    contentInsetRight_ = right;
    contentInsetBottom_ = bottom;
  }
  void ClearBlockedGameRegions();
  void SetBlockedGameRegion(int index, const TouchBlockedRegion &region);
  void QueuePlaceTap(float x, float y);
  void RequestCameraBaseline(float x, float y);

private:
  struct PointerState
  {
    bool active{false};
    bool gamePointer{false};
    bool tapCandidate{false};
    bool lookZoneTouch{false};
    bool lookDrag{false};
    glm::vec2 startPos{0.f, 0.f};
    glm::vec2 lastLookPos{0.f, 0.f};
    std::chrono::steady_clock::time_point downTime{};
  };

  void ApplyJoystickToKeys();
  int NormalizePointerId(int pointerId) const;
  float ContentLeft() const { return static_cast<float>(contentInsetLeft_); }
  float ContentRight() const
  {
    return static_cast<float>(screenWidth_ - contentInsetRight_);
  }
  float ContentTop() const { return static_cast<float>(contentInsetTop_); }
  float ContentBottom() const
  {
    return static_cast<float>(screenHeight_ - contentInsetBottom_);
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

  glm::vec2 joystick_{0.f};
  glm::vec2 mousePosition_{0.f};
  glm::vec2 mouseDelta_{0.f};
  glm::vec2 pendingPlacePos_{0.f};
  glm::vec2 cameraBaselinePos_{0.f};
  std::array<PointerState, kMaxPointers> pointers_{};
  std::array<TouchBlockedRegion, kMaxBlockedRegions> blockedRegions_{};
  int screenWidth_{1280};
  int screenHeight_{720};
  int contentInsetLeft_{0};
  int contentInsetTop_{0};
  int contentInsetRight_{0};
  int contentInsetBottom_{0};
  float uiScale_{1.f};
  float placeClickMaxSeconds_{0.20f};
  float breakHoldMinSeconds_{0.50f};
  bool keys_[512]{};
  bool prevKeys_[512]{};
  bool mouseButtons_[8]{};
  bool prevMouseButtons_[8]{};
  bool mouseJustPressed_[8]{};
  bool mouseJustReleased_[8]{};
  bool manualKeys_[512]{};
  bool joystickActive_{false};
  bool pendingPlaceTap_{false};
  bool cameraBaselinePending_{false};
  bool blockInputCancelPending_{false};
};

} // namespace cutum

#endif
