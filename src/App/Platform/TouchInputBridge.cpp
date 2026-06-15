#include "App/Platform/TouchInputBridge.h"

#include <algorithm>
#include <cmath>

namespace cutum
{

namespace
{

constexpr int kKeyIndex(KeyCode key) { return static_cast<int>(key); }
constexpr float kLookDragThresholdPx = 10.f;
constexpr float kTapCancelDragPx = 24.f;

} // namespace

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
  joystick_ = {0.f, 0.f};
  mouseDelta_ = {0.f, 0.f};
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
  mousePosition_ = {x, y};

  if (!gameInput || IsBlockedGameInput(x, y))
  {
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
  const float dragDistance =
      glm::length(pos - pointer.startPos);

  if (pointer.tapCandidate && !pointer.lookDrag)
  {
    if (IsInLookZone(pointer.startPos.x) && dragDistance > kLookDragThresholdPx)
    {
      pointer.lookDrag = true;
      pointer.tapCandidate = false;
      mouseButtons_[static_cast<int>(MouseButton::Left)] = false;
    }
    else if (!IsInLookZone(pointer.startPos.x) &&
             dragDistance > kTapCancelDragPx)
    {
      pointer.tapCandidate = false;
      mouseButtons_[static_cast<int>(MouseButton::Left)] = false;
    }
  }

  if (allowLook && pointer.lookDrag)
  {
    mouseDelta_ += glm::vec2(x - pointer.lastLookPos.x, y - pointer.lastLookPos.y);
    pointer.lastLookPos = {x, y};
  }

  mousePosition_ = {x, y};
}

void TouchInputBridge::OnTouchUp(int pointerId, float x, float y)
{
  const int id = NormalizePointerId(pointerId);
  PointerState &pointer = pointers_[id];
  mousePosition_ = {x, y};

  if (pointer.active && pointer.gamePointer && pointer.tapCandidate)
  {
    mouseJustReleased_[static_cast<int>(MouseButton::Left)] = true;
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
  keys_[kKeyIndex(KeyCode::Key_W)] =
      manualKeys_[kKeyIndex(KeyCode::Key_W)] || joystick_.y > 0.25f;
  keys_[kKeyIndex(KeyCode::Key_S)] =
      manualKeys_[kKeyIndex(KeyCode::Key_S)] || joystick_.y < -0.25f;
  keys_[kKeyIndex(KeyCode::Key_A)] =
      manualKeys_[kKeyIndex(KeyCode::Key_A)] || joystick_.x < -0.25f;
  keys_[kKeyIndex(KeyCode::Key_D)] =
      manualKeys_[kKeyIndex(KeyCode::Key_D)] || joystick_.x > 0.25f;
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
  keys_[idx] = pressed;
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
