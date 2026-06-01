#include "GuiSlot.h"
#include "Gui/GuiRenderer.h"
#include "Gui/GuiTheme.h"

namespace cutum {

GuiSlot::GuiSlot(const GuiTheme* theme, int size)
    : theme_(theme)
    , slotSize_(size)
{
    bounds_.w = size;
    bounds_.h = size;
}

int GuiSlot::GetPreferredWidth() const { return slotSize_; }
int GuiSlot::GetPreferredHeight() const { return slotSize_; }

void GuiSlot::Draw(GuiRenderer& renderer)
{
    if (!visible_ || !theme_) {
        return;
    }
    renderer.DrawFilledRect(bounds_, theme_->slotBackground);
    if (selected_) {
        renderer.DrawBorderRect(bounds_, theme_->slotSelected, 2);
    } else {
        renderer.DrawBorderRect(bounds_, theme_->panelBorder, theme_->borderThickness);
    }
    if (!label_.empty()) {
        renderer.DrawText(label_, bounds_.x + 2, bounds_.y + bounds_.h - theme_->fontSizeBody - 2,
                          theme_->textSecondary);
    }
}

bool GuiSlot::OnMouseDown(const GuiMouseEvent& event)
{
    if (!enabled_ || !visible_ || !bounds_.Contains(event.x, event.y)) {
        return false;
    }
    pressed_ = true;
    return true;
}

bool GuiSlot::OnMouseUp(const GuiMouseEvent& event)
{
    if (!enabled_ || !pressed_) {
        return false;
    }
    pressed_ = false;
    if (bounds_.Contains(event.x, event.y) && onClick_) {
        onClick_();
        return true;
    }
    return false;
}

} // namespace cutum
