#include "GuiTabBar.h"
#include "Gui/GuiRenderer.h"
#include "Gui/GuiTheme.h"

namespace cutum
{

UGuiTabBar::UGuiTabBar(const GuiTheme *theme) : theme_(theme) {}

void UGuiTabBar::SetTabs(std::vector<std::string> labels)
{
  labels_ = std::move(labels);
  if (activeTab_ >= static_cast<int>(labels_.size()))
  {
    activeTab_ = labels_.empty() ? 0 : static_cast<int>(labels_.size()) - 1;
  }
}

void UGuiTabBar::SetActiveTab(int tab)
{
  if (tab >= 0 && tab < static_cast<int>(labels_.size()))
  {
    activeTab_ = tab;
  }
}

void UGuiTabBar::SetOnTabChanged(std::function<void(int)> handler)
{
  onTabChanged_ = std::move(handler);
}

int UGuiTabBar::GetPreferredHeight() const
{
  return theme_ ? theme_->fontSizeBody + theme_->padding * 2 : 28;
}

void UGuiTabBar::Draw(UGuiRenderer &renderer)
{
  if (!visible_ || !theme_ || labels_.empty())
  {
    return;
  }
  const int tabW = bounds_.w / static_cast<int>(labels_.size());
  for (size_t i = 0; i < labels_.size(); ++i)
  {
    GuiRect tabRect{bounds_.x + static_cast<int>(i) * tabW, bounds_.y, tabW,
                    bounds_.h};
    const glm::vec4 color = static_cast<int>(i) == activeTab_
                                ? theme_->buttonHover
                                : theme_->buttonNormal;
    renderer.DrawFilledRect(tabRect, color);
    renderer.DrawText(labels_[i], tabRect.x + theme_->padding,
                      tabRect.y + theme_->padding, theme_->textPrimary);
  }
}

bool UGuiTabBar::OnMouseDown(const GuiMouseEvent &event)
{
  if (!visible_ || !bounds_.Contains(event.x, event.y) || labels_.empty())
  {
    return false;
  }
  const int tabW = bounds_.w / static_cast<int>(labels_.size());
  const int index = (event.x - bounds_.x) / tabW;
  if (index >= 0 && index < static_cast<int>(labels_.size()))
  {
    activeTab_ = index;
    if (onTabChanged_)
    {
      onTabChanged_(index);
    }
    return true;
  }
  return false;
}

} // namespace cutum
