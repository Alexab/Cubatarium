#include "GuiWindow.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Core/GuiTheme.h"

namespace cutum
{

UGuiWindow::UGuiWindow(const GuiTheme *theme, std::string title)
    : UGuiPanel(theme), title_(std::move(title))
{
}

GuiRect UGuiWindow::GetClientArea() const
{
  return {bounds_.x, bounds_.y + kTitleBarHeight, bounds_.w,
          std::max(0, bounds_.h - kTitleBarHeight)};
}

void UGuiWindow::Draw(UGuiRenderer &renderer)
{
  if (!visible_)
  {
    return;
  }
  UGuiPanel::Draw(renderer);
  if (!theme_)
  {
    return;
  }
  GuiRect titleBar{bounds_.x, bounds_.y, bounds_.w, kTitleBarHeight};
  renderer.DrawFilledRect(titleBar, theme_->buttonPressed);
  renderer.DrawText(title_, titleBar.x + theme_->padding, titleBar.y + 4,
                    theme_->textPrimary);
  UGuiWidget::Draw(renderer);
}

} // namespace cutum
