#include "App/Platform/TouchInputBridge.h"

#include <algorithm>
#include <cmath>

namespace cutum
{

namespace
{

constexpr int kKeyIndex(KeyCode key) { return static_cast<int>(key); }

} // namespace

void TouchInputBridge::Reset()
{
  std::fill(std::begin(keys_), std::end(keys_), false);
  std::fill(std::begin(prevKeys_), std::end(prevKeys_), false);
  std::fill(std::begin(manualKeys_), std::end(manualKeys_), false);
  std::fill(std::begin(mouseButtons_), std::end(mouseButtons_), false);
  std::fill(std::begin(prevMouseButtons_), std::end(prevMouseButtons_), false);
  std::fill(std::begin(mouseJustPressed_), std::end(mouseJustPressed_), false);
  joystick_ = {0.f, 0.f};
  mouseDelta_ = {0.f, 0.f};
  lookActive_ = false;
}

void TouchInputBridge::OnTouchDown(int pointerId, float x, float y)
{
  (void)pointerId;
  mousePosition_ = {x, y};
  const bool rightSide = x > screenWidth_ * 0.5f;
  const bool leftSide = x < screenWidth_ * 0.2f;
  if (rightSide)
  {
    lookActive_ = true;
    lastLookPos_ = {x, y};
  }
  if (!leftSide && !rightSide)
  {
    mouseButtons_[static_cast<int>(MouseButton::Left)] = true;
    mouseJustPressed_[static_cast<int>(MouseButton::Left)] = true;
  }
}

void TouchInputBridge::OnTouchMove(int pointerId, float x, float y)
{
  (void)pointerId;
  if (lookActive_ && x > screenWidth_ * 0.5f)
  {
    mouseDelta_ += glm::vec2(x - lastLookPos_.x, y - lastLookPos_.y);
    lastLookPos_ = {x, y};
  }
  mousePosition_ = {x, y};
}

void TouchInputBridge::OnTouchUp(int pointerId, float x, float y)
{
  (void)pointerId;
  mousePosition_ = {x, y};
  if (x > screenWidth_ * 0.5f)
  {
    lookActive_ = false;
  }
  mouseButtons_[static_cast<int>(MouseButton::Left)] = false;
  mouseButtons_[static_cast<int>(MouseButton::Right)] = false;
}

void TouchInputBridge::Update()
{
  std::copy(std::begin(keys_), std::end(keys_), std::begin(prevKeys_));
  std::copy(std::begin(mouseButtons_), std::end(mouseButtons_),
            std::begin(prevMouseButtons_));
  std::fill(std::begin(mouseJustPressed_), std::end(mouseJustPressed_), false);
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
