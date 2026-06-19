#ifndef GUI_PANEL_H
#define GUI_PANEL_H

#include "Gui/Widgets/GuiWidget.h"
#include <glm/glm.hpp>

namespace cutum
{

struct GuiTheme;

class UGuiPanel : public UGuiWidget
{
public:
  explicit UGuiPanel(const GuiTheme *theme);

  void Draw(UGuiRenderer &renderer) override;

  void SetDrawBackground(bool draw)
  {
    DrawBackground = draw;
    SetClipChildren(draw);
  }
  bool GetDrawBackground() const { return DrawBackground; }

  void SetStackLayout(int spacing, int Padding);
  int GetPreferredHeight() const override;

protected:
  const GuiTheme *Theme;
  bool DrawBackground{true};
  int StackSpacing{6};
  int StackPadding{0};
};

} // namespace cutum

#endif
