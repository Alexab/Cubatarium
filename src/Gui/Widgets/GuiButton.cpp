#include "GuiButton.h"
#include "Gui/GuiFocus.h"
#include "Gui/GuiRenderer.h"
#include "Gui/GuiTheme.h"

namespace cutum {

GuiButton::GuiButton(const GuiTheme* theme, std::string label)
    : theme_(theme)
    , label_(std::move(label))
{
}

int GuiButton::GetPreferredHeight() const
{
    return theme_ ? theme_->fontSizeBody + theme_->padding * 2 : 32;
}

bool GuiButton::CanFocus() const
{
    return enabled_ && visible_;
}

bool GuiButton::Activate()
{
    if (!CanFocus() || !onClick_) {
        return false;
    }
    onClick_();
    return true;
}

glm::vec4 GuiButton::StateColor() const
{
    if (!theme_) {
        return glm::vec4(0.3f);
    }
    if (!enabled_) {
        return theme_->buttonDisabled;
    }
    switch (state_) {
    case GuiButtonState::Hovered:
        return theme_->buttonHover;
    case GuiButtonState::Pressed:
        return theme_->buttonPressed;
    default:
        return theme_->buttonNormal;
    }
}

void GuiButton::Draw(GuiRenderer& renderer)
{
    if (!visible_ || !theme_) {
        return;
    }
    renderer.DrawFilledRect(bounds_, StateColor());
    renderer.DrawBorderRect(bounds_, theme_->panelBorder, theme_->borderThickness);
    GuiRect textRect = bounds_;
    if (label_.size() == 1) {
        if (label_[0] == '+') {
            textRect.y += 2;
        } else if (label_[0] == '-') {
            textRect.y += 3;
        }
    }
    renderer.DrawTextCenteredInRect(textRect, label_, theme_->textPrimary);
    if (HasFocusHighlight()) {
        DrawWidgetFocusRing(renderer, *theme_, bounds_);
    }
}

bool GuiButton::OnMouseDown(const GuiMouseEvent& event)
{
    if (!enabled_ || !visible_ || !bounds_.Contains(event.x, event.y)) {
        return false;
    }
    if (event.button != GuiMouseButton::Left) {
        return false;
    }
    state_ = GuiButtonState::Pressed;
    pressedInside_ = true;
    return true;
}

bool GuiButton::OnMouseUp(const GuiMouseEvent& event)
{
    if (!enabled_ || !visible_) {
        return false;
    }
    const bool inside = bounds_.Contains(event.x, event.y);
    const bool wasPressed = pressedInside_;
    if (wasPressed && event.button == GuiMouseButton::Left && onClick_) {
        onClick_();
    }
    pressedInside_ = false;
    state_ = inside ? GuiButtonState::Hovered : GuiButtonState::Normal;
    return wasPressed || inside;
}

bool GuiButton::OnMouseMove(const GuiMouseEvent& event)
{
    if (!visible_) {
        return false;
    }
    const bool inside = bounds_.Contains(event.x, event.y);
    if (!enabled_) {
        state_ = GuiButtonState::Disabled;
        return inside;
    }
    if (state_ != GuiButtonState::Pressed) {
        state_ = inside ? GuiButtonState::Hovered : GuiButtonState::Normal;
    }
    return inside;
}

} // namespace cutum
