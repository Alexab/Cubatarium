#include "GuiPanel.h"
#include "Gui/GuiRenderer.h"
#include "Gui/GuiTheme.h"
#include "Gui/Layout/GuiLayout.h"

namespace cutum
{

UGuiPanel::UGuiPanel(const GuiTheme *theme) : theme_(theme) {}

void UGuiPanel::SetStackLayout(int spacing, int padding)
{
  stackSpacing_ = spacing;
  stackPadding_ = padding;
}

int UGuiPanel::GetPreferredHeight() const
{
  std::vector<UGuiWidget *> kids;
  for (const auto &child : children_)
  {
    if (child->IsVisible())
    {
      kids.push_back(child.get());
    }
  }
  if (kids.empty())
  {
    return UGuiWidget::GetPreferredHeight();
  }
  const int w = bounds_.w > 0 ? bounds_.w : 400;
  return UGuiLayout::StackVerticalMeasure({0, 0, w, 100000}, stackSpacing_,
                                          stackPadding_, kids);
}

void UGuiPanel::Draw(UGuiRenderer &renderer)
{
  if (!visible_ || !theme_)
  {
    return;
  }
  if (drawBackground_)
  {
    renderer.DrawFilledRect(bounds_, theme_->panelBackground);
    renderer.DrawBorderRect(bounds_, theme_->panelBorder,
                            theme_->borderThickness);
  }
  UGuiWidget::Draw(renderer);
}

} // namespace cutum
