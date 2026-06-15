#ifndef GUI_TOUCH_CONTROLS_H
#define GUI_TOUCH_CONTROLS_H

#include <functional>

namespace cutum
{

class TouchInputBridge;
class UGuiPanel;
class UGuiWidget;
struct GuiTheme;

class GuiTouchControls
{
public:
  GuiTouchControls(const GuiTheme *theme, TouchInputBridge *bridge,
                   std::function<void()> onMenu,
                   std::function<void()> onInventory,
                   std::function<void()> onConsole,
                   std::function<void()> onJumpPress);

  void Build(UGuiPanel *parent);
  void Layout(int width, int height, int offsetX, int offsetY,
              float uiScale = 1.f);
  bool RouteCapturedMove(int pointerId, int x, int y);
  void ReleaseJoystickCapture();
  void ReleaseAllCaptures();
  ~GuiTouchControls();

private:
  const GuiTheme *theme_{nullptr};
  TouchInputBridge *bridge_{nullptr};
  std::function<void()> onMenu_;
  std::function<void()> onInventory_;
  std::function<void()> onConsole_;
  std::function<void()> onJumpPress_;
  UGuiPanel *root_{nullptr};
  UGuiWidget *joystick_{nullptr};
  UGuiWidget *lookPad_{nullptr};
  UGuiWidget *jumpButton_{nullptr};
  UGuiWidget *sneakButton_{nullptr};
  UGuiWidget *inventoryButton_{nullptr};
  UGuiWidget *menuButton_{nullptr};
  UGuiWidget *consoleButton_{nullptr};
  float uiScale_{1.f};
  std::function<bool(int, int, int)> routeCapturedMove_;
  std::function<void()> releaseJoystickCapture_;
  std::function<void()> releaseAllCaptures_;
};

} // namespace cutum

#endif
