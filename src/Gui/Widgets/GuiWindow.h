#ifndef GUI_WINDOW_H
#define GUI_WINDOW_H

#include "Gui/Widgets/GuiPanel.h"
#include <string>

namespace cutum
{

class UGuiWindow : public UGuiPanel
{
public:
  UGuiWindow(const GuiTheme *theme, std::string title);

  GuiRect GetClientArea() const;
  void SetChromeVisible(bool visible) { ChromeVisible = visible; }
  void SetTitle(const std::string &title) { Title = title; }
  void Draw(UGuiRenderer &renderer) override;

private:
  std::string Title;
  bool ChromeVisible{true};
};

} // namespace cutum

#endif
