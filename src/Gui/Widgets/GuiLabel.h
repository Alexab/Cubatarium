#ifndef GUI_LABEL_H
#define GUI_LABEL_H

#include "Gui/Widgets/GuiWidget.h"
#include <string>

namespace cutum
{

struct GuiTheme;

enum class GuiTextAlign
{
  Left,
  Center
};

class UGuiLabel : public UGuiWidget
{
public:
  UGuiLabel(const GuiTheme *theme, std::string text);

  void SetText(const std::string &text) { Text = text; }
  const std::string &GetText() const { return Text; }
  void SetTextAlign(GuiTextAlign align) { TextAlign = align; }
  void SetDrawBackground(bool draw) { DrawBackground = draw; }
  void SetUseSecondaryColor(bool use) { UseSecondaryColor = use; }
  /// Debug info badge: black text on a white box hugging the text; the box is
  /// pinned to the left or right edge of Bounds.
  void SetInfoBadgeStyle(bool enable, bool alignRight = false)
  {
    InfoBadge = enable;
    InfoBadgeAlignRight = alignRight;
  }

  void Draw(UGuiRenderer &renderer) override;
  int GetPreferredHeight() const override;

private:
  const GuiTheme *Theme;
  std::string Text;
  GuiTextAlign TextAlign{GuiTextAlign::Left};
  bool DrawBackground{false};
  bool UseSecondaryColor{false};
  bool InfoBadge{false};
  bool InfoBadgeAlignRight{false};
};

} // namespace cutum

#endif
