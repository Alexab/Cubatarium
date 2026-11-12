#include "Gui/Widgets/GuiTabBar.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Core/GuiTheme.h"

namespace cutum
{

UGuiTabBar::UGuiTabBar(const GuiTheme *theme) : Theme(theme) {}

void UGuiTabBar::SetTabs(std::vector<std::string> labels)
{
  Labels = std::move(labels);
  if (Enabled.size() != Labels.size())
  {
    Enabled.assign(Labels.size(), true);
  }
  if (ActiveTab >= static_cast<int>(Labels.size()))
  {
    ActiveTab = Labels.empty() ? 0 : static_cast<int>(Labels.size()) - 1;
  }
}

void UGuiTabBar::SetTabEnabled(std::vector<bool> enabled)
{
  Enabled = std::move(enabled);
  if (Enabled.size() < Labels.size())
  {
    Enabled.resize(Labels.size(), true);
  }
}

void UGuiTabBar::SetActiveTab(int tab)
{
  if (tab >= 0 && tab < static_cast<int>(Labels.size()))
  {
    ActiveTab = tab;
  }
}

void UGuiTabBar::SetOnTabChanged(std::function<void(int)> handler)
{
  OnTabChanged = std::move(handler);
}

int UGuiTabBar::GetPreferredHeight() const
{
  return Theme ? Theme->FontSizeBody + Theme->Padding * 2 : 28;
}

void UGuiTabBar::Draw(UGuiRenderer &renderer)
{
  if (!Visible || !Theme || Labels.empty())
  {
    return;
  }
  const int tabW = Bounds.W / static_cast<int>(Labels.size());
  for (size_t i = 0; i < Labels.size(); ++i)
  {
    GuiRect tabRect{Bounds.X + static_cast<int>(i) * tabW, Bounds.Y, tabW,
                    Bounds.H};
    const bool enabled =
        i >= Enabled.size() || Enabled[i];
    glm::vec4 color = Theme->ButtonNormal;
    if (!enabled)
    {
      color = Theme->ButtonDisabled;
    }
    else if (static_cast<int>(i) == ActiveTab)
    {
      color = Theme->ButtonHover;
    }
    renderer.DrawFilledRect(tabRect, color);
    const glm::vec3 text =
        enabled ? Theme->TextPrimary : Theme->TextSecondary;
    renderer.DrawText(Labels[i], tabRect.X + Theme->Padding,
                      tabRect.Y + Theme->Padding, text);
  }
}

bool UGuiTabBar::OnMouseDown(const GuiMouseEvent &event)
{
  if (!Visible || !Bounds.Contains(event.X, event.Y) || Labels.empty())
  {
    return false;
  }
  const int tabW = Bounds.W / static_cast<int>(Labels.size());
  const int index = (event.X - Bounds.X) / tabW;
  if (index >= 0 && index < static_cast<int>(Labels.size()))
  {
    if (index < static_cast<int>(Enabled.size()) && !Enabled[static_cast<size_t>(index)])
    {
      return true;
    }
    ActiveTab = index;
    if (OnTabChanged)
    {
      OnTabChanged(index);
    }
    return true;
  }
  return false;
}

} // namespace cutum
