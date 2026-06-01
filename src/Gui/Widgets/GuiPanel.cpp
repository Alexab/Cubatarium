#include "GuiPanel.h"
#include "Gui/GuiRenderer.h"
#include "Gui/GuiTheme.h"

namespace cutum {

GuiPanel::GuiPanel(const GuiTheme* theme)
    : theme_(theme)
{
}

void GuiPanel::Draw(GuiRenderer& renderer)
{
    if (!visible_ || !theme_) {
        return;
    }
    if (drawBackground_) {
        renderer.DrawFilledRect(bounds_, theme_->panelBackground);
        renderer.DrawBorderRect(bounds_, theme_->panelBorder, theme_->borderThickness);
    }
    GuiWidget::Draw(renderer);
}

} // namespace cutum
