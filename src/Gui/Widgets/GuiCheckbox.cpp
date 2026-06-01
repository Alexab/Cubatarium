#include "GuiCheckbox.h"
#include "Gui/GuiFocus.h"
#include "Gui/GuiRenderer.h"
#include "Gui/GuiTheme.h"

namespace cutum {

GuiCheckbox::GuiCheckbox(const GuiTheme* theme, std::string label)
    : theme_(theme)
    , label_(std::move(label))
{
}

int GuiCheckbox::GetPreferredHeight() const
{
    return theme_ ? theme_->fontSizeBody + theme_->padding : 24;
}

bool GuiCheckbox::CanFocus() const
{
    return enabled_ && visible_;
}

bool GuiCheckbox::Activate()
{
    if (!CanFocus()) {
        return false;
    }
    checked_ = !checked_;
    if (onChanged_) {
        onChanged_(checked_);
    }
    return true;
}

void GuiCheckbox::Draw(GuiRenderer& renderer)
{
    if (!visible_ || !theme_) {
        return;
    }
    const int box = theme_->fontSizeBody;
    GuiRect boxRect{bounds_.x, bounds_.y + (bounds_.h - box) / 2, box, box};
    renderer.DrawFilledRect(boxRect, checked_ ? theme_->buttonHover : theme_->buttonNormal);
    renderer.DrawBorderRect(boxRect, theme_->panelBorder, theme_->borderThickness);
    if (checked_) {
        renderer.DrawTextCenteredInRect(boxRect, "x", theme_->textPrimary);
    }
    renderer.DrawText(label_, bounds_.x + box + theme_->padding, bounds_.y + theme_->padding / 2,
                      theme_->textPrimary);
    if (HasFocusHighlight()) {
        DrawWidgetFocusRing(renderer, *theme_, bounds_);
    }
}

bool GuiCheckbox::OnMouseDown(const GuiMouseEvent& event)
{
    if (!enabled_ || !visible_ || !bounds_.Contains(event.x, event.y)) {
        return false;
    }
    if (event.button != GuiMouseButton::Left) {
        return false;
    }
    return Activate();
}

} // namespace cutum
