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
                   std::function<void()> onInventory);

  void Build(UGuiPanel *parent);
  void Layout(int width, int height);
  ~GuiTouchControls();

private:
  const GuiTheme *theme_{nullptr};
  TouchInputBridge *bridge_{nullptr};
  std::function<void()> onMenu_;
  std::function<void()> onInventory_;
  UGuiPanel *root_{nullptr};
  UGuiWidget *jumpButton_{nullptr};
  UGuiWidget *sneakButton_{nullptr};
  UGuiWidget *inventoryButton_{nullptr};
  UGuiWidget *menuButton_{nullptr};
};

} // namespace cutum

#endif
