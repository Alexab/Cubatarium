#include "GuiTextInput.h"
#include "Gui/GuiRenderer.h"
#include "Gui/GuiTheme.h"

#include <GLFW/glfw3.h>
#include <algorithm>

namespace cutum {

GuiTextInput::GuiTextInput(const GuiTheme* theme)
    : theme_(theme)
{
}

int GuiTextInput::GetPreferredHeight() const
{
    return theme_ ? theme_->fontSizeBody + theme_->padding * 2 : 28;
}

void GuiTextInput::SetText(const std::string& text)
{
    buffer_ = text;
    caretPos_ = buffer_.size();
}

void GuiTextInput::Draw(GuiRenderer& renderer)
{
    if (!visible_ || !theme_) {
        return;
    }
    renderer.DrawFilledRect(bounds_, theme_->buttonNormal);
    renderer.DrawBorderRect(bounds_, focused_ ? theme_->slotSelected : theme_->panelBorder,
                            theme_->borderThickness);
    std::string display = buffer_;
    if (focused_) {
        display += '|';
    }
    renderer.DrawText(display, bounds_.x + theme_->padding, bounds_.y + theme_->padding,
                      theme_->textPrimary);
}

bool GuiTextInput::OnMouseDown(const GuiMouseEvent& event)
{
    if (!visible_ || !bounds_.Contains(event.x, event.y)) {
        focused_ = false;
        return false;
    }
    focused_ = true;
    return true;
}

bool GuiTextInput::OnChar(const GuiCharEvent& event)
{
    if (!focused_ || !enabled_) {
        return false;
    }
    if (event.codepoint >= 32 && event.codepoint < 127) {
        buffer_.insert(caretPos_, 1, static_cast<char>(event.codepoint));
        ++caretPos_;
        return true;
    }
    return false;
}

bool GuiTextInput::OnKey(const GuiKeyEvent& event)
{
    if (!focused_ || !enabled_ || event.action != GuiKeyAction::Press) {
        return false;
    }
    if (event.keyCode == GLFW_KEY_BACKSPACE) {
        if (caretPos_ > 0) {
            buffer_.erase(caretPos_ - 1, 1);
            --caretPos_;
        }
        return true;
    }
    if (event.keyCode == GLFW_KEY_LEFT && caretPos_ > 0) {
        --caretPos_;
        return true;
    }
    if (event.keyCode == GLFW_KEY_RIGHT && caretPos_ < buffer_.size()) {
        ++caretPos_;
        return true;
    }
    return false;
}

} // namespace cutum
