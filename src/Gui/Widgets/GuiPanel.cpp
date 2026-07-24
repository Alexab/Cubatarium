#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Core/GuiTheme.h"
#include "Gui/Layout/GuiLayout.h"

namespace cutum
{

UGuiPanel::UGuiPanel(const GuiTheme *theme) : Theme(theme)
{
  SetClipChildren(true);
}

void UGuiPanel::SetStackLayout(int spacing, int Padding)
{
  StackSpacing = spacing;
  StackPadding = Padding;
}

int UGuiPanel::GetPreferredHeight() const
{
  std::vector<UGuiWidget *> kids;
  for (const auto &child : Children)
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
  const int w = Bounds.W > 0 ? Bounds.W : 400;
  return UGuiLayout::StackVerticalMeasure({0, 0, w, 100000}, StackSpacing,
                                          StackPadding, kids);
}

void UGuiPanel::Draw(UGuiRenderer &renderer)
{
  if (!Visible || !Theme)
  {
    return;
  }
  if (DrawBackground)
  {
    renderer.DrawFilledRect(Bounds, Theme->PanelBackground);
    renderer.DrawBorderRect(Bounds, Theme->PanelBorder, Theme->BorderThickness);
  }
  UGuiWidget::Draw(renderer);
}

} // namespace cutum
