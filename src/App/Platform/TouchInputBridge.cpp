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

float UTouchInputBridge::LookDragThresholdPx() const
{
  return kLookDragThresholdBasePx * UiScale;
}

float UTouchInputBridge::PlaceTapSlopPx() const
{
  return std::min(kPlaceTapSlopBasePx * UiScale, 56.f);
}

int UTouchInputBridge::NormalizePointerId(int PointerId) const
{
  if (PointerId < 0)
  {
    return 0;
  }
  if (PointerId >= kMaxPointers)
  {
    return kMaxPointers - 1;
  }
  return PointerId;
}

void UTouchInputBridge::ClearBlockedGameRegions()
{
  for (TouchBlockedRegion &region : BlockedRegions)
  {
    region = TouchBlockedRegion{};
  }
}

void UTouchInputBridge::SetBlockedGameRegion(int index,
                                             const TouchBlockedRegion &region)
{
  if (index < 0 || index >= kMaxBlockedRegions)
  {
    return;
  }
  BlockedRegions[static_cast<size_t>(index)] = region;
}

bool UTouchInputBridge::IsBlockedGameInput(float x, float y) const
{
  if (x < ContentLeft() || x > ContentRight() || y < ContentTop() ||
      y > ContentBottom())
  {
    return true;
  }
  for (const TouchBlockedRegion &region : BlockedRegions)
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

void UTouchInputBridge::Reset()
{
  std::fill(std::begin(Keys), std::end(Keys), false);
  std::fill(std::begin(PrevKeys), std::end(PrevKeys), false);
  std::fill(std::begin(ManualKeys), std::end(ManualKeys), false);
  std::fill(std::begin(MouseButtons), std::end(MouseButtons), false);
  std::fill(std::begin(PrevMouseButtons), std::end(PrevMouseButtons), false);
  std::fill(std::begin(MouseJustPressed), std::end(MouseJustPressed), false);
  std::fill(std::begin(MouseJustReleased), std::end(MouseJustReleased), false);
  for (PointerState &pointer : Pointers)
  {
    pointer = PointerState{};
  }
  ResetJoystick();
  MouseDelta = {0.f, 0.f};
  PendingPlaceTap = false;
  CameraBaselinePending = false;
  BlockInputCancelPending = false;
}

bool UTouchInputBridge::ConsumeBlockInputCancel()
{
  if (!BlockInputCancelPending)
  {
    return false;
  }
  BlockInputCancelPending = false;
  return true;
}

void UTouchInputBridge::ResetJoystick()
{
  Joystick = {0.f, 0.f};
  JoystickActive = false;
}

void UTouchInputBridge::SetJoystickActive(bool Active)
{
  if (Active && !JoystickActive)
  {
    BlockInputCancelPending = true;
  }
  JoystickActive = Active;
}

bool UTouchInputBridge::IsMovementBlockingBreak() const
{
  if (!JoystickActive)
  {
    return false;
  }
  return glm::length(Joystick) > 0.15f;
}

void UTouchInputBridge::AddLookDelta(float dx, float dy)
{
  MouseDelta += glm::vec2(dx, dy);
}

void UTouchInputBridge::RequestCameraBaseline(float x, float y)
{
  QueueCameraBaseline(x, y);
}

void UTouchInputBridge::QueueCameraBaseline(float x, float y)
{
  CameraBaselinePos = {x, y};
  CameraBaselinePending = true;
}

bool UTouchInputBridge::ConsumeCameraBaseline(float &outX, float &outY)
{
  if (!CameraBaselinePending)
  {
    return false;
  }
  outX = CameraBaselinePos.x;
  outY = CameraBaselinePos.y;
  CameraBaselinePending = false;
  return true;
}

bool UTouchInputBridge::ConsumePendingPlaceTap(glm::vec2 &outPos)
{
  if (!PendingPlaceTap)
  {
    return false;
  }
  outPos = PendingPlacePos;
  PendingPlaceTap = false;
  return true;
}

void UTouchInputBridge::QueuePlaceTap(float x, float y)
{
  PendingPlaceTap = true;
  PendingPlacePos = {x, y};
}

void UTouchInputBridge::OnTouchDown(int PointerId, float x, float y,
                                    bool gameInput)
{
  const int Id = NormalizePointerId(PointerId);
  PointerState &pointer = Pointers[Id];
  pointer = PointerState{};
  pointer.Active = true;
  pointer.GamePointer = gameInput;
  pointer.StartPos = {x, y};
  pointer.LastLookPos = {x, y};
  pointer.DownTime = std::chrono::steady_clock::now();
  MousePosition = {x, y};

  if (!gameInput || IsBlockedGameInput(x, y))
  {
    return;
  }

  if (IsInLookZone(x))
  {
    pointer.LookZoneTouch = true;
    pointer.LastLookPos = {x, y};
    QueueCameraBaseline(x, y);
    return;
  }

  pointer.TapCandidate = true;
  MouseButtons[static_cast<int>(MouseButton::Left)] = true;
  MouseJustPressed[static_cast<int>(MouseButton::Left)] = true;
}

void UTouchInputBridge::OnTouchMove(int PointerId, float x, float y,
                                    bool allowLook)
{
  const int Id = NormalizePointerId(PointerId);
  PointerState &pointer = Pointers[Id];
  if (!pointer.Active || !pointer.GamePointer)
  {
    MousePosition = {x, y};
    return;
  }

  const glm::vec2 pos{x, y};
  const float dragDistance = glm::length(pos - pointer.StartPos);

  if (!pointer.LookDrag)
  {
    const bool lookGesture =
        pointer.LookZoneTouch ||
        (pointer.TapCandidate && IsInLookZone(pointer.StartPos.x));
    if (lookGesture && dragDistance > LookDragThresholdPx())
    {
      pointer.LookDrag = true;
      pointer.TapCandidate = false;
      MouseButtons[static_cast<int>(MouseButton::Left)] = false;
      BlockInputCancelPending = true;
      QueueCameraBaseline(x, y);
    }
  }

  if (allowLook && pointer.LookDrag)
  {
    MouseDelta +=
        glm::vec2(x - pointer.LastLookPos.x, y - pointer.LastLookPos.y);
    pointer.LastLookPos = {x, y};
  }

  MousePosition = {x, y};
}

void UTouchInputBridge::OnTouchUp(int PointerId, float x, float y,
                                  bool cancelled)
{
  const int Id = NormalizePointerId(PointerId);
  PointerState &pointer = Pointers[Id];
  MousePosition = {x, y};

  if (!cancelled && pointer.Active && pointer.GamePointer &&
      !pointer.LookDrag &&
      !IsBlockedGameInput(pointer.StartPos.x, pointer.StartPos.y))
  {
    const glm::vec2 pos{x, y};
    const float dragDistance = glm::length(pos - pointer.StartPos);
    const float holdSeconds =
        std::chrono::duration<float>(std::chrono::steady_clock::now() -
                                     pointer.DownTime)
            .count();
    const bool tapLike = pointer.TapCandidate || pointer.LookZoneTouch;

    if (tapLike && dragDistance < PlaceTapSlopPx() &&
        holdSeconds < BreakHoldMinSeconds)
    {
      PendingPlaceTap = true;
      PendingPlacePos = pos;
    }
    else if (pointer.TapCandidate)
    {
      MouseJustReleased[static_cast<int>(MouseButton::Left)] = true;
    }
  }

  MouseButtons[static_cast<int>(MouseButton::Left)] = false;
  MouseButtons[static_cast<int>(MouseButton::Right)] = false;
  pointer = PointerState{};
}

void UTouchInputBridge::Update()
{
  std::copy(std::begin(Keys), std::end(Keys), std::begin(PrevKeys));
  std::copy(std::begin(MouseButtons), std::end(MouseButtons),
            std::begin(PrevMouseButtons));
  std::fill(std::begin(MouseJustPressed), std::end(MouseJustPressed), false);
  std::fill(std::begin(MouseJustReleased), std::end(MouseJustReleased), false);
  ApplyJoystickToKeys();
}

void UTouchInputBridge::ApplyJoystickToKeys()
{
  const bool joyForward = JoystickActive && Joystick.y > 0.25f;
  const bool joyBack = JoystickActive && Joystick.y < -0.25f;
  const bool joyLeft = JoystickActive && Joystick.x < -0.25f;
  const bool joyRight = JoystickActive && Joystick.x > 0.25f;

  Keys[kKeyIndex(KeyCode::Key_W)] =
      ManualKeys[kKeyIndex(KeyCode::Key_W)] || joyForward;
  Keys[kKeyIndex(KeyCode::Key_S)] =
      ManualKeys[kKeyIndex(KeyCode::Key_S)] || joyBack;
  Keys[kKeyIndex(KeyCode::Key_A)] =
      ManualKeys[kKeyIndex(KeyCode::Key_A)] || joyLeft;
  Keys[kKeyIndex(KeyCode::Key_D)] =
      ManualKeys[kKeyIndex(KeyCode::Key_D)] || joyRight;
  Keys[kKeyIndex(KeyCode::Key_Space)] =
      ManualKeys[kKeyIndex(KeyCode::Key_Space)];
  Keys[kKeyIndex(KeyCode::Key_Shift)] =
      ManualKeys[kKeyIndex(KeyCode::Key_Shift)];
}

void UTouchInputBridge::SetHeldKey(KeyCode key, bool Pressed)
{
  const int idx = kKeyIndex(key);
  if (idx < 0 || idx >= 512)
  {
    return;
  }
  ManualKeys[idx] = Pressed;
  ApplyJoystickToKeys();
}

glm::vec2 UTouchInputBridge::ConsumeMouseDelta()
{
  const glm::vec2 delta = MouseDelta;
  MouseDelta = {0.f, 0.f};
  return delta;
}

bool UTouchInputBridge::IsMouseButtonJustReleased(MouseButton Button) const
{
  const int idx = static_cast<int>(Button);
  if (idx >= 0 && idx < 8 && MouseJustReleased[idx])
  {
    return true;
  }
  return idx >= 0 && idx < 8 && !MouseButtons[idx] && PrevMouseButtons[idx];
}

bool UTouchInputBridge::IsKeyPressed(KeyCode key) const
{
  const int idx = kKeyIndex(key);
  if (idx < 0 || idx >= 512)
  {
    return false;
  }
  return Keys[idx];
}

bool UTouchInputBridge::IsMouseButtonPressed(MouseButton Button) const
{
  const int idx = static_cast<int>(Button);
  return idx >= 0 && idx < 8 && MouseButtons[idx];
}

bool UTouchInputBridge::IsMouseButtonJustPressed(MouseButton Button) const
{
  const int idx = static_cast<int>(Button);
  return idx >= 0 && idx < 8 && MouseJustPressed[idx];
}

} // namespace cutum
