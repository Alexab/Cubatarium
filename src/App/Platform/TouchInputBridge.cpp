#include "App/Platform/TouchInputBridge.h"

#include <algorithm>
#include <cmath>

namespace cutum
{

namespace
{

constexpr int kKeyIndex(KeyCode key) { return static_cast<int>(key); }
constexpr float kLookDragThresholdBasePx = 10.f;
constexpr float kPlaceTapSlopBasePx = 36.f;

} // namespace

float TouchInputBridge::LookDragThresholdPx() const
{
  return kLookDragThresholdBasePx * uiScale_;
}

float TouchInputBridge::PlaceTapSlopPx() const
{
  return std::min(kPlaceTapSlopBasePx * uiScale_, 56.f);
}

int TouchInputBridge::NormalizePointerId(int pointerId) const
{
  if (pointerId < 0)
  {
    return 0;
  }
  if (pointerId >= kMaxPointers)
  {
    return kMaxPointers - 1;
  }
  return pointerId;
}

void TouchInputBridge::ClearBlockedGameRegions()
{
  for (TouchBlockedRegion &region : blockedRegions_)
  {
    region = TouchBlockedRegion{};
  }
}

void TouchInputBridge::SetBlockedGameRegion(int index,
                                            const TouchBlockedRegion &region)
{
  if (index < 0 || index >= kMaxBlockedRegions)
  {
    return;
  }
  blockedRegions_[static_cast<size_t>(index)] = region;
}

bool TouchInputBridge::IsBlockedGameInput(float x, float y) const
{
  if (x < ContentLeft() || x > ContentRight() || y < ContentTop() ||
      y > ContentBottom())
  {
    return true;
  }
  for (const TouchBlockedRegion &region : blockedRegions_)
  {
    if (region.w <= 0 || region.h <= 0)
    {
      continue;
    }
    if (x >= static_cast<float>(region.x) &&
        x < static_cast<float>(region.x + region.w) &&
        y >= static_cast<float>(region.y) &&
        y < static_cast<float>(region.y + region.h))
    {
      return true;
    }
  }
  return false;
}

void TouchInputBridge::Reset()
{
  std::fill(std::begin(keys_), std::end(keys_), false);
  std::fill(std::begin(prevKeys_), std::end(prevKeys_), false);
  std::fill(std::begin(manualKeys_), std::end(manualKeys_), false);
  std::fill(std::begin(mouseButtons_), std::end(mouseButtons_), false);
  std::fill(std::begin(prevMouseButtons_), std::end(prevMouseButtons_), false);
  std::fill(std::begin(mouseJustPressed_), std::end(mouseJustPressed_), false);
  std::fill(std::begin(mouseJustReleased_), std::end(mouseJustReleased_), false);
  for (PointerState &pointer : pointers_)
  {
    pointer = PointerState{};
  }
  ResetJoystick();
  mouseDelta_ = {0.f, 0.f};
  pendingPlaceTap_ = false;
  cameraBaselinePending_ = false;
  blockInputCancelPending_ = false;
}

bool TouchInputBridge::ConsumeBlockInputCancel()
{
  if (!blockInputCancelPending_)
  {
    return false;
  }
  blockInputCancelPending_ = false;
  return true;
}

void TouchInputBridge::ResetJoystick()
{
  joystick_ = {0.f, 0.f};
  joystickActive_ = false;
}

void TouchInputBridge::SetJoystickActive(bool active)
{
  if (active && !joystickActive_)
  {
    blockInputCancelPending_ = true;
  }
  joystickActive_ = active;
}

bool TouchInputBridge::IsMovementBlockingBreak() const
{
  if (!joystickActive_)
  {
    return false;
  }
  return glm::length(joystick_) > 0.15f;
}

void TouchInputBridge::AddLookDelta(float dx, float dy)
{
  mouseDelta_ += glm::vec2(dx, dy);
}

void TouchInputBridge::RequestCameraBaseline(float x, float y)
{
  QueueCameraBaseline(x, y);
}

void TouchInputBridge::QueueCameraBaseline(float x, float y)
{
  cameraBaselinePos_ = {x, y};
  cameraBaselinePending_ = true;
}

bool TouchInputBridge::ConsumeCameraBaseline(float &outX, float &outY)
{
  if (!cameraBaselinePending_)
  {
    return false;
  }
  outX = cameraBaselinePos_.x;
  outY = cameraBaselinePos_.y;
  cameraBaselinePending_ = false;
  return true;
}

bool TouchInputBridge::ConsumePendingPlaceTap(glm::vec2 &outPos)
{
  if (!pendingPlaceTap_)
  {
    return false;
  }
  outPos = pendingPlacePos_;
  pendingPlaceTap_ = false;
  return true;
}

void TouchInputBridge::QueuePlaceTap(float x, float y)
{
  pendingPlaceTap_ = true;
  pendingPlacePos_ = {x, y};
}

void TouchInputBridge::OnTouchDown(int pointerId, float x, float y,
                                   bool gameInput)
{
  const int id = NormalizePointerId(pointerId);
  PointerState &pointer = pointers_[id];
  pointer = PointerState{};
  pointer.active = true;
  pointer.gamePointer = gameInput;
  pointer.startPos = {x, y};
  pointer.lastLookPos = {x, y};
  pointer.downTime = std::chrono::steady_clock::now();
  mousePosition_ = {x, y};

  if (!gameInput || IsBlockedGameInput(x, y))
  {
    return;
  }

  if (IsInLookZone(x))
  {
    pointer.lookZoneTouch = true;
    pointer.lastLookPos = {x, y};
    QueueCameraBaseline(x, y);
    return;
  }

  pointer.tapCandidate = true;
  mouseButtons_[static_cast<int>(MouseButton::Left)] = true;
  mouseJustPressed_[static_cast<int>(MouseButton::Left)] = true;
}

void TouchInputBridge::OnTouchMove(int pointerId, float x, float y,
                                   bool allowLook)
{
  const int id = NormalizePointerId(pointerId);
  PointerState &pointer = pointers_[id];
  if (!pointer.active || !pointer.gamePointer)
  {
    mousePosition_ = {x, y};
    return;
  }

  const glm::vec2 pos{x, y};
  const float dragDistance = glm::length(pos - pointer.startPos);

  if (!pointer.lookDrag)
  {
    const bool lookGesture =
        pointer.lookZoneTouch ||
        (pointer.tapCandidate && IsInLookZone(pointer.startPos.x));
    if (lookGesture && dragDistance > LookDragThresholdPx())
    {
      pointer.lookDrag = true;
      pointer.tapCandidate = false;
      mouseButtons_[static_cast<int>(MouseButton::Left)] = false;
      blockInputCancelPending_ = true;
      QueueCameraBaseline(x, y);
    }
  }

  if (allowLook && pointer.lookDrag)
  {
    mouseDelta_ +=
        glm::vec2(x - pointer.lastLookPos.x, y - pointer.lastLookPos.y);
    pointer.lastLookPos = {x, y};
  }

  mousePosition_ = {x, y};
}

void TouchInputBridge::OnTouchUp(int pointerId, float x, float y,
                                 bool cancelled)
{
  const int id = NormalizePointerId(pointerId);
  PointerState &pointer = pointers_[id];
  mousePosition_ = {x, y};

  if (!cancelled && pointer.active && pointer.gamePointer && !pointer.lookDrag &&
      !IsBlockedGameInput(pointer.startPos.x, pointer.startPos.y))
  {
    const glm::vec2 pos{x, y};
    const float dragDistance = glm::length(pos - pointer.startPos);
    const float holdSeconds = std::chrono::duration<float>(
                                  std::chrono::steady_clock::now() -
                                  pointer.downTime)
                                  .count();
    const bool tapLike = pointer.tapCandidate || pointer.lookZoneTouch;

    if (tapLike && dragDistance < PlaceTapSlopPx() &&
        holdSeconds < breakHoldMinSeconds_)
    {
      pendingPlaceTap_ = true;
      pendingPlacePos_ = pos;
    }
    else if (pointer.tapCandidate)
    {
      mouseJustReleased_[static_cast<int>(MouseButton::Left)] = true;
    }
  }

  mouseButtons_[static_cast<int>(MouseButton::Left)] = false;
  mouseButtons_[static_cast<int>(MouseButton::Right)] = false;
  pointer = PointerState{};
}

void TouchInputBridge::Update()
{
  std::copy(std::begin(keys_), std::end(keys_), std::begin(prevKeys_));
  std::copy(std::begin(mouseButtons_), std::end(mouseButtons_),
            std::begin(prevMouseButtons_));
  std::fill(std::begin(mouseJustPressed_), std::end(mouseJustPressed_), false);
  std::fill(std::begin(mouseJustReleased_), std::end(mouseJustReleased_), false);
  ApplyJoystickToKeys();
}

void TouchInputBridge::ApplyJoystickToKeys()
{
  const bool joyForward = joystickActive_ && joystick_.y > 0.25f;
  const bool joyBack = joystickActive_ && joystick_.y < -0.25f;
  const bool joyLeft = joystickActive_ && joystick_.x < -0.25f;
  const bool joyRight = joystickActive_ && joystick_.x > 0.25f;

  keys_[kKeyIndex(KeyCode::Key_W)] =
      manualKeys_[kKeyIndex(KeyCode::Key_W)] || joyForward;
  keys_[kKeyIndex(KeyCode::Key_S)] =
      manualKeys_[kKeyIndex(KeyCode::Key_S)] || joyBack;
  keys_[kKeyIndex(KeyCode::Key_A)] =
      manualKeys_[kKeyIndex(KeyCode::Key_A)] || joyLeft;
  keys_[kKeyIndex(KeyCode::Key_D)] =
      manualKeys_[kKeyIndex(KeyCode::Key_D)] || joyRight;
  keys_[kKeyIndex(KeyCode::Key_Space)] =
      manualKeys_[kKeyIndex(KeyCode::Key_Space)];
  keys_[kKeyIndex(KeyCode::Key_Shift)] =
      manualKeys_[kKeyIndex(KeyCode::Key_Shift)];
}

void TouchInputBridge::SetHeldKey(KeyCode key, bool pressed)
{
  const int idx = kKeyIndex(key);
  if (idx < 0 || idx >= 512)
  {
    return;
  }
  manualKeys_[idx] = pressed;
  ApplyJoystickToKeys();
}

glm::vec2 TouchInputBridge::ConsumeMouseDelta()
{
  const glm::vec2 delta = mouseDelta_;
  mouseDelta_ = {0.f, 0.f};
  return delta;
}

bool TouchInputBridge::IsMouseButtonJustReleased(MouseButton button) const
{
  const int idx = static_cast<int>(button);
  if (idx >= 0 && idx < 8 && mouseJustReleased_[idx])
  {
    return true;
  }
  return idx >= 0 && idx < 8 && !mouseButtons_[idx] && prevMouseButtons_[idx];
}

bool TouchInputBridge::IsKeyPressed(KeyCode key) const
{
  const int idx = kKeyIndex(key);
  if (idx < 0 || idx >= 512)
  {
    return false;
  }
  return keys_[idx];
}

bool TouchInputBridge::IsMouseButtonPressed(MouseButton button) const
{
  const int idx = static_cast<int>(button);
  return idx >= 0 && idx < 8 && mouseButtons_[idx];
}

bool TouchInputBridge::IsMouseButtonJustPressed(MouseButton button) const
{
  const int idx = static_cast<int>(button);
  return idx >= 0 && idx < 8 && mouseJustPressed_[idx];
}

} // namespace cutum
