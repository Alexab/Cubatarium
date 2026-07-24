#ifndef GUI_TOUCH_CONTROLS_H
#define GUI_TOUCH_CONTROLS_H

#include <functional>

#include "Gui/Core/GuiTypes.h"

namespace cutum
{

class UTouchInputBridge;
class UGuiPanel;
class UGuiWidget;
class UGuiRenderer;
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
  void Layout(int width, int height, int offsetX, int offsetY);
  bool RouteCapturedMove(int PointerId, int x, int y);
  void ReleaseJoystickCapture();
  void ReleaseJoystickCaptureForPointer(int pointer_id);
  void ReleaseAllCaptures();
  bool HitTestTopRightReserved(int x, int y) const;
  GuiRect GetTopRightReservedRect() const;
  void RenderOverlay(class UGuiRenderer &renderer);
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
  int LastLayoutWidth{-1};
  int LastLayoutHeight{-1};
  int LastLayoutOffsetX{-1};
  int LastLayoutOffsetY{-1};
  float LastLayoutUiScale{-1.f};
  GuiRect TopRightReservedRect{};
  int TopRightScreenOffsetX{0};
  int TopRightScreenOffsetY{0};
  std::function<bool(int, int, int)> OnRouteCapturedMove;
  std::function<void()> OnReleaseJoystickCapture;
  std::function<void()> OnReleaseHoldButtons;
  std::function<void()> OnReleaseAllCaptures;
};

} // namespace cutum

#endif
