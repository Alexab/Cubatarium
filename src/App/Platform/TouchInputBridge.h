#ifndef TOUCH_INPUT_BRIDGE_H
#define TOUCH_INPUT_BRIDGE_H

#include "App/Platform/InputManager.h"
#include <glm/glm.hpp>

namespace cutum
{

class TouchInputBridge
{
public:
  void Reset();
  void OnTouchDown(int pointerId, float x, float y);
  void OnTouchMove(int pointerId, float x, float y);
  void OnTouchUp(int pointerId, float x, float y);
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
  void SetScreenSize(int w, int h)
  {
    screenWidth_ = w;
    screenHeight_ = h;
  }

private:
  void ApplyJoystickToKeys();
  glm::vec2 joystick_{0.f};
  glm::vec2 mousePosition_{0.f};
  glm::vec2 mouseDelta_{0.f};
  glm::vec2 lastLookPos_{0.f};
  bool lookActive_{false};
  int screenWidth_{1280};
  int screenHeight_{720};
  bool keys_[512]{};
  bool prevKeys_[512]{};
  bool mouseButtons_[8]{};
  bool prevMouseButtons_[8]{};
  bool mouseJustPressed_[8]{};
  bool manualKeys_[512]{};
};

} // namespace cutum

#endif
