#include "App/Platform/InputManager.h"
#include "App/Platform/TouchInputBridge.h"

#include <cstdlib>
#include <iostream>

namespace
{

constexpr const char *kTestName = "touch_input_bridge_lifecycle_test";

void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << kTestName << ": " << message << std::endl;
    std::exit(1);
  }
}

void TestPointerIdNormalization()
{
  cutum::UTouchInputBridge bridge;
  Expect(bridge.NormalizePointerId(3) == 3, "stable pointer id preserved");
  Expect(bridge.NormalizePointerId(-1) == 0, "negative pointer id maps to 0");
  Expect(bridge.NormalizePointerId(99) ==
             cutum::UTouchInputBridge::kMaxPointers - 1,
         "oversized pointer id clamps to max slot");
}

void TestJoystickResetOnCancel()
{
  cutum::UTouchInputBridge bridge;
  bridge.SetJoystickActive(true);
  bridge.SetJoystickVector({0.8f, 0.6f});
  bridge.Update();
  Expect(bridge.IsKeyPressed(cutum::KeyCode::Key_W),
         "joystick forward maps to movement key");
  bridge.Reset();
  bridge.Update();
  Expect(!bridge.IsJoystickActive(), "reset clears joystick active flag");
  Expect(!bridge.IsKeyPressed(cutum::KeyCode::Key_W),
         "reset clears joystick-driven movement");
}

void TestTouchUpOutsideZoneClearsPointer()
{
  cutum::UTouchInputBridge bridge;
  bridge.SetScreenSize(1280, 720);
  bridge.OnTouchDown(7, 120.f, 500.f, true);
  bridge.OnTouchMove(7, 40.f, 520.f, true);
  bridge.OnTouchUp(7, 40.f, 520.f, false);
  bridge.Update();
  Expect(!bridge.IsMouseButtonPressed(cutum::MouseButton::Left),
         "pointer up clears mouse button state");
}

void TestMultitouchPointerIsolation()
{
  cutum::UTouchInputBridge bridge;
  bridge.SetScreenSize(1280, 720);
  bridge.OnTouchDown(2, 120.f, 500.f, true);
  bridge.OnTouchDown(5, 900.f, 200.f, true);
  bridge.OnTouchUp(2, 40.f, 520.f, false);
  bridge.Update();
  Expect(!bridge.IsMouseButtonPressed(cutum::MouseButton::Left),
         "releasing one pointer clears left button latch");
}

void TestJoystickAndHeldKeyIsolation()
{
  cutum::UTouchInputBridge bridge;
  bridge.SetJoystickActive(true);
  bridge.SetJoystickVector({0.8f, 0.6f});
  bridge.SetHeldKey(cutum::KeyCode::Key_Space, true);
  bridge.Update();
  Expect(bridge.IsJoystickActive(), "joystick active while jump held");
  Expect(bridge.IsKeyPressed(cutum::KeyCode::Key_W),
         "joystick forward while jump held");

  bridge.SetHeldKey(cutum::KeyCode::Key_Space, false);
  bridge.Update();
  Expect(bridge.IsJoystickActive(), "joystick survives jump release");
  Expect(bridge.IsKeyPressed(cutum::KeyCode::Key_W),
         "joystick movement survives jump release");

  bridge.ResetJoystick();
  bridge.Update();
  Expect(!bridge.IsJoystickActive(), "joystick reset clears active flag");
  Expect(!bridge.IsKeyPressed(cutum::KeyCode::Key_W),
         "joystick reset clears movement");
}

} // namespace

int main()
{
  TestPointerIdNormalization();
  TestJoystickResetOnCancel();
  TestTouchUpOutsideZoneClearsPointer();
  TestMultitouchPointerIsolation();
  TestJoystickAndHeldKeyIsolation();
  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}
