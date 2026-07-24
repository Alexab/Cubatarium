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

struct TouchIsoControlCallbacks
{
  std::function<bool()> IsActive;
  std::function<void(int)> SnapYaw;
  std::function<void(float)> Zoom;
  std::function<void()> CycleView;
};

class UGuiTouchControls
{
public:
  UGuiTouchControls(const GuiTheme *theme, UTouchInputBridge *bridge,
                    std::function<void()> onMenu,
                    std::function<void()> onInventory,
                    std::function<void()> onConsole,
                    std::function<void()> onJumpPress,
                    TouchIsoControlCallbacks isoCallbacks = {});

  void Build(UGuiPanel *parent);
  void Layout(int width, int height, int offsetX, int offsetY);
  void InvalidateLayout();
  bool RouteCapturedMove(int PointerId, int x, int y);
  void ReleaseJoystickCapture();
  void ReleaseJoystickCaptureForPointer(int pointer_id);
  void ReleaseAllCaptures();
  bool HitTestTopRightReserved(int x, int y) const;
  GuiRect GetTopRightReservedRect() const;
  void RenderOverlay(class UGuiRenderer &renderer);
  ~UGuiTouchControls();

private:
  bool IsoControlsEnabled() const;

  const GuiTheme *Theme{nullptr};
  UTouchInputBridge *Bridge{nullptr};
  std::function<void()> OnMenu;
  std::function<void()> OnInventory;
  std::function<void()> OnConsole;
  std::function<void()> OnJumpPress;
  TouchIsoControlCallbacks IsoCallbacks;
  UGuiPanel *Root{nullptr};
  UGuiWidget *JoystickWidget{nullptr};
  UGuiWidget *LookPad{nullptr};
  UGuiWidget *JumpButton{nullptr};
  UGuiWidget *SneakButton{nullptr};
  UGuiWidget *SprintButton{nullptr};
  UGuiWidget *InventoryButton{nullptr};
  UGuiWidget *MenuButton{nullptr};
  UGuiWidget *ConsoleButton{nullptr};
  UGuiWidget *IsoSnapLeftButton{nullptr};
  UGuiWidget *IsoSnapRightButton{nullptr};
  UGuiWidget *IsoZoomInButton{nullptr};
  UGuiWidget *IsoZoomOutButton{nullptr};
  UGuiWidget *IsoCycleViewButton{nullptr};
  float UiScale{1.f};
  int LastLayoutWidth{-1};
  int LastLayoutHeight{-1};
  int LastLayoutOffsetX{-1};
  int LastLayoutOffsetY{-1};
  float LastLayoutUiScale{-1.f};
  bool LastIsoEnabled{false};
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
