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
        renderer.DrawFilledRect(bounds_, theme_->slotSelectedFill);
    }

    if (iconTexture_ != 0) {
        const int inset = 4;
        const GuiRect iconRect{bounds_.x + inset, bounds_.y + inset, bounds_.w - inset * 2,
                               bounds_.h - inset * 2};
        renderer.DrawTexturedRect(iconRect, iconTexture_);
    }

    if (selected_) {
        renderer.DrawBorderRect(bounds_, theme_->slotSelected, theme_->slotSelectedBorderThickness);
        const GuiRect inner = bounds_.Inset(2);
        renderer.DrawBorderRect(inner, theme_->slotSelectedInner, 1);
    } else {
        renderer.DrawBorderRect(bounds_, theme_->panelBorder, theme_->borderThickness);
    }

    if (!cornerHint_.empty()) {
        const glm::vec3 textColor = selected_ ? glm::vec3(0.95f, 0.95f, 0.95f) : theme_->textSecondary;
        renderer.DrawText(cornerHint_, bounds_.x + 4, bounds_.y + 2, textColor);
    }
}

bool GuiSlot::OnMouseDown(const GuiMouseEvent& event)
{
    if (!enabled_ || !visible_ || !bounds_.Contains(event.x, event.y)) {
        return false;
    }
    pressed_ = true;
    if (onClick_) {
        onClick_();
    }
    return true;
}

bool GuiSlot::OnMouseUp(const GuiMouseEvent& event)
{
    if (!enabled_ || !pressed_) {
        return false;
    }
    pressed_ = false;
    return bounds_.Contains(event.x, event.y);
}

} // namespace cutum
