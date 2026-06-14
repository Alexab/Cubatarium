#include "GuiSlot.h"
#include "Gui/GuiRenderer.h"
#include "Gui/GuiTheme.h"

#include <cmath>

namespace cutum {

namespace {
constexpr int kDragThresholdPx = 8;
}

UGuiSlot::UGuiSlot(const GuiTheme* theme, int size)
    : theme_(theme)
    , slotSize_(size)
{
    bounds_.w = size;
    bounds_.h = size;
}

int UGuiSlot::GetPreferredWidth() const { return slotSize_; }
int UGuiSlot::GetPreferredHeight() const { return slotSize_; }

void UGuiSlot::Draw(UGuiRenderer& renderer)
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

bool UGuiSlot::OnMouseDown(const GuiMouseEvent& event)
{
    if (!enabled_ || !visible_ || !bounds_.Contains(event.x, event.y)) {
        return false;
    }
    pressed_ = true;
    dragStarted_ = false;
    pressX_ = event.x;
    pressY_ = event.y;
    return true;
}

bool UGuiSlot::OnMouseUp(const GuiMouseEvent& event)
{
    if (!enabled_ || !pressed_) {
        return false;
    }
    const int dx = event.x - pressX_;
    const int dy = event.y - pressY_;
    const bool isClickGesture =
        (dx * dx + dy * dy) <= (kDragThresholdPx * kDragThresholdPx);
    if (isClickGesture && onClick_) {
        onClick_();
    }
    pressed_ = false;
    dragStarted_ = false;
    return true;
}

bool UGuiSlot::OnMouseMove(const GuiMouseEvent& event)
{
    if (!pressed_ || dragStarted_) {
        return pressed_;
    }
    const int dx = event.x - pressX_;
    const int dy = event.y - pressY_;
    if ((dx * dx + dy * dy) > (kDragThresholdPx * kDragThresholdPx)) {
        dragStarted_ = true;
        if (onBeginDrag_) {
            onBeginDrag_();
        }
    }
    return true;
}

} // namespace cutum
