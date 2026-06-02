#include "GuiPanel.h"
#include "Gui/GuiRenderer.h"
#include "Gui/GuiTheme.h"
#include "Gui/Layout/GuiLayout.h"

namespace cutum {

GuiPanel::GuiPanel(const GuiTheme* theme)
    : theme_(theme)
{
}

void GuiPanel::SetStackLayout(int spacing, int padding)
{
    stackSpacing_ = spacing;
    stackPadding_ = padding;
}

int GuiPanel::GetPreferredHeight() const
{
    std::vector<GuiWidget*> kids;
    for (const auto& child : children_) {
        if (child->IsVisible()) {
            kids.push_back(child.get());
        }
    }
    if (kids.empty()) {
        return GuiWidget::GetPreferredHeight();
    }
    const int w = bounds_.w > 0 ? bounds_.w : 400;
    return GuiLayout::StackVerticalMeasure({0, 0, w, 100000}, stackSpacing_, stackPadding_, kids);
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
