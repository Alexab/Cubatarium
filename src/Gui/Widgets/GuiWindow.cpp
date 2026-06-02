#include "GuiWindow.h"
#include "Gui/GuiRenderer.h"
#include "Gui/GuiTheme.h"

namespace cutum {

GuiWindow::GuiWindow(const GuiTheme* theme, std::string title)
    : GuiPanel(theme)
    , title_(std::move(title))
{
}

GuiRect GuiWindow::GetClientArea() const
{
    return {bounds_.x, bounds_.y + kTitleBarHeight, bounds_.w,
            std::max(0, bounds_.h - kTitleBarHeight)};
}

void GuiWindow::Draw(GuiRenderer& renderer)
{
    if (!visible_) {
        return;
    }
    GuiPanel::Draw(renderer);
    if (!theme_) {
        return;
    }
    GuiRect titleBar{bounds_.x, bounds_.y, bounds_.w, kTitleBarHeight};
    renderer.DrawFilledRect(titleBar, theme_->buttonPressed);
    renderer.DrawText(title_, titleBar.x + theme_->padding, titleBar.y + 4, theme_->textPrimary);
    GuiWidget::Draw(renderer);
}

} // namespace cutum
