#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Core/GuiTheme.h"

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
  return Theme->FontSizeBody + Theme->Padding;
}

void UGuiLabel::Draw(UGuiRenderer &renderer)
{
  if (!Visible || !Theme || Text.empty())
  {
    return;
  }
  if (DrawBackground)
  {
    const glm::vec4 &bg = BackgroundOverride ? *BackgroundOverride
                                             : Theme->TooltipBackground;
    renderer.DrawFilledRect(Bounds, bg);
  }
  const glm::vec3 &color =
      UseSecondaryColor ? Theme->TextSecondary : Theme->TextPrimary;
  if (TextAlign == GuiTextAlign::Center)
  {
    renderer.DrawTextCenteredInRect(Bounds, Text, color);
  }
  else
  {
    renderer.DrawText(Text, Bounds.X + Theme->Padding,
                      Bounds.Y + Theme->Padding, color);
  }
}

} // namespace cutum
