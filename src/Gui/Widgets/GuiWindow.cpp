#include "Gui/Widgets/GuiWindow.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Core/GuiTheme.h"

namespace cutum
{

UGuiWindow::UGuiWindow(const GuiTheme *theme, std::string title)
    : UGuiPanel(theme), Title(std::move(title))
{
}

GuiRect UGuiWindow::GetClientArea() const
{
  return {Bounds.X, Bounds.Y + kTitleBarHeight, Bounds.W,
          std::max(0, Bounds.H - kTitleBarHeight)};
}

void UGuiWindow::Draw(UGuiRenderer &renderer)
{
  if (!Visible)
  {
    return;
  }
  UGuiPanel::Draw(renderer);
  if (!Theme)
  {
    return;
  }
  GuiRect titleBar{Bounds.X, Bounds.Y, Bounds.W, kTitleBarHeight};
  renderer.DrawFilledRect(titleBar, Theme->ButtonPressed);
  renderer.DrawText(Title, titleBar.X + Theme->Padding, titleBar.Y + 4,
                    Theme->TextPrimary);
  UGuiWidget::Draw(renderer);
}

} // namespace cutum
