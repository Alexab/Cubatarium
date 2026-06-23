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
  if (!Visible || !Theme)
  {
    return;
  }
  if (DrawBackground)
  {
    renderer.DrawFilledRect(Bounds, Theme->PanelBackground);
    renderer.DrawBorderRect(Bounds, Theme->PanelBorder, Theme->BorderThickness);
  }
  const GuiRect titleBar{Bounds.X, Bounds.Y, Bounds.W, kTitleBarHeight};
  renderer.DrawFilledRect(titleBar, Theme->ButtonPressed);
  renderer.DrawText(Title, titleBar.X + Theme->Padding, titleBar.Y + 4,
                    Theme->TextPrimary);

  const GuiRect client = GetClientArea();
  const bool clipClient = client.W > 0 && client.H > 0;
  if (clipClient)
  {
    renderer.PushClipRect(client);
  }
  UGuiWidget::Draw(renderer);
  if (clipClient)
  {
    renderer.PopClipRect();
  }
}

} // namespace cutum
