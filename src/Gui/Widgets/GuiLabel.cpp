#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Core/GuiTheme.h"

#include <algorithm>

namespace cutum
{

UGuiLabel::UGuiLabel(const GuiTheme *theme, std::string text)
    : Theme(theme), Text(std::move(text))
{
}

int UGuiLabel::GetPreferredHeight() const
{
  if (!Theme)
  {
    return UGuiWidget::GetPreferredHeight();
  }
  int lines = 1;
  for (char c : Text)
  {
    if (c == '\n')
    {
      ++lines;
    }
  }
  const int lineH = Theme->FontSizeBody + std::max(2, Theme->Padding / 2);
  return lines * lineH + Theme->Padding;
}

void UGuiLabel::Draw(UGuiRenderer &renderer)
{
  if (!Visible || !Theme || Text.empty())
  {
    return;
  }
  if (DrawBackground)
  {
    renderer.DrawFilledRect(Bounds, Theme->TooltipBackground);
  }
  const glm::vec3 &color =
      UseSecondaryColor ? Theme->TextSecondary : Theme->TextPrimary;
  const int lineH = Theme->FontSizeBody + std::max(2, Theme->Padding / 2);
  const int startX = Bounds.X + Theme->Padding;
  int y = Bounds.Y + Theme->Padding;

  size_t start = 0;
  while (start <= Text.size())
  {
    const size_t nl = Text.find('\n', start);
    const std::string line =
        Text.substr(start, nl == std::string::npos ? std::string::npos
                                                   : nl - start);
    if (TextAlign == GuiTextAlign::Center)
    {
      GuiRect lineRect{Bounds.X, y, Bounds.W, lineH};
      renderer.DrawTextCenteredInRect(lineRect, line, color);
    }
    else
    {
      renderer.DrawText(line, startX, y, color);
    }
    if (nl == std::string::npos)
    {
      break;
    }
    start = nl + 1;
    y += lineH;
  }
}

} // namespace cutum
