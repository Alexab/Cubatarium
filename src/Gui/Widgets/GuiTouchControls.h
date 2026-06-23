#ifndef GUI_TOUCH_CONTROLS_H
#define GUI_TOUCH_CONTROLS_H

#include <functional>

namespace cutum
{

class UTouchInputBridge;
class UGuiPanel;
class UGuiWidget;
struct GuiTheme;

class UGuiTouchControls
{
public:
  UGuiTouchControls(const GuiTheme *theme, UTouchInputBridge *bridge,
                    std::function<void()> onMenu,
                    std::function<void()> onInventory,
                    std::function<void()> onConsole,
                    std::function<void()> onJumpPress);

  void Build(UGuiPanel *parent);
  void Layout(int width, int height, int offsetX, int offsetY,
              float uiScale = 1.f);
  bool RouteCapturedMove(int PointerId, int x, int y);
  void ReleaseJoystickCapture();
  void ReleaseAllCaptures();
  ~UGuiTouchControls();

private:
  const GuiTheme *Theme{nullptr};
  UTouchInputBridge *Bridge{nullptr};
  std::function<void()> OnMenu;
  std::function<void()> OnInventory;
  std::function<void()> OnConsole;
  std::function<void()> OnJumpPress;
  UGuiPanel *Root{nullptr};
  UGuiWidget *JoystickWidget{nullptr};
  UGuiWidget *LookPad{nullptr};
  UGuiWidget *JumpButton{nullptr};
  UGuiWidget *SneakButton{nullptr};
  UGuiWidget *SprintButton{nullptr};
  UGuiWidget *InventoryButton{nullptr};
  UGuiWidget *MenuButton{nullptr};
  UGuiWidget *ConsoleButton{nullptr};
  float UiScale{1.f};
  int lastLayoutWidth_{-1};
  int lastLayoutHeight_{-1};
  int lastLayoutOffsetX_{-1};
  int lastLayoutOffsetY_{-1};
  float lastLayoutUiScale_{-1.f};
  std::function<bool(int, int, int)> OnRouteCapturedMove;
  std::function<void()> OnReleaseJoystickCapture;
  std::function<void()> OnReleaseHoldButtons;
  std::function<void()> OnReleaseAllCaptures;
};

} // namespace cutum

#endif
