#ifndef GUI_PANEL_H
#define GUI_PANEL_H

#include "GuiWidget.h"
#include <glm/glm.hpp>

namespace cutum
{

struct GuiTheme;

class UGuiPanel : public UGuiWidget
{
public:
  explicit UGuiPanel(const GuiTheme *theme);

  void Draw(UGuiRenderer &renderer) override;

  void SetDrawBackground(bool draw) { drawBackground_ = draw; }
  bool GetDrawBackground() const { return drawBackground_; }

  void SetStackLayout(int spacing, int padding);
  int GetPreferredHeight() const override;

protected:
  const GuiTheme *theme_;
  bool drawBackground_{true};
  int stackSpacing_{6};
  int stackPadding_{0};
};

} // namespace cutum

#endif
