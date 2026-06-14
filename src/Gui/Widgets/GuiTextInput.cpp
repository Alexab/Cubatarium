#include "GuiTextInput.h"
#include "ConsoleInputSanitize.h"
#include "Gui/GuiFocus.h"
#include "Gui/GuiRenderer.h"
#include "Gui/GuiTheme.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cctype>
#include <glm/glm.hpp>

namespace cutum {

UGuiTextInput::UGuiTextInput(const GuiTheme* theme)
    : theme_(theme)
{
}

int UGuiTextInput::GetPreferredHeight() const
{
    return theme_ ? theme_->fontSizeBody + theme_->padding * 2 : 28;
}

int UGuiTextInput::TextPadding() const
{
    return theme_ ? theme_->padding : 8;
}

int UGuiTextInput::TextLeft() const
{
    return bounds_.x + TextPadding();
}

bool UGuiTextInput::CanFocus() const
{
    return enabled_ && visible_;
}

size_t UGuiTextInput::SelMin() const
{
    return std::min(selAnchor_, selEnd_);
}

size_t UGuiTextInput::SelMax() const
{
    return std::max(selAnchor_, selEnd_);
}

bool UGuiTextInput::HasSelection() const
{
    return SelMin() < SelMax();
}

void UGuiTextInput::ClearSelection()
{
    selAnchor_ = selEnd_ = caretPos_;
}

void UGuiTextInput::NotifyEdited()
{
    if (!programmaticChange_ && onEdited_) {
        onEdited_();
    }
}

void UGuiTextInput::SetText(const std::string& text)
{
    programmaticChange_ = true;
    buffer_ = SanitizeConsoleLine(text);
    if (buffer_.size() > kMaxLength) {
        buffer_.resize(kMaxLength);
    }
    caretPos_ = buffer_.size();
    ClearSelection();
    programmaticChange_ = false;
}

void UGuiTextInput::DeleteSelection()
{
    if (!HasSelection()) {
        return;
    }
    const size_t a = SelMin();
    const size_t b = SelMax();
    buffer_.erase(a, b - a);
    caretPos_ = a;
    ClearSelection();
}

void UGuiTextInput::InsertText(const std::string& text)
{
    if (text.empty()) {
        return;
    }
    DeleteSelection();
    const size_t room = kMaxLength > buffer_.size() ? kMaxLength - buffer_.size() : 0;
    std::string chunk = text.substr(0, room);
    buffer_.insert(caretPos_, chunk);
    caretPos_ += chunk.size();
    NotifyEdited();
}

size_t UGuiTextInput::CaretIndexFromX(int mouseX, UGuiRenderer& renderer) const
{
    const int left = TextLeft();
    const int relX = mouseX - left;
    if (relX <= 0) {
        return 0;
    }
    size_t lo = 0;
    size_t hi = buffer_.size();
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (renderer.MeasureTextWidth(buffer_.substr(0, mid)) < relX) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return std::min(lo, buffer_.size());
}

std::string UGuiTextInput::GetSelectedText() const
{
    if (!HasSelection()) {
        return {};
    }
    return buffer_.substr(SelMin(), SelMax() - SelMin());
}

void UGuiTextInput::SelectAll()
{
    selAnchor_ = 0;
    selEnd_ = buffer_.size();
    caretPos_ = buffer_.size();
}

void UGuiTextInput::CopySelectionToClipboard()
{
    if (!Clipboard) {
        return;
    }
    std::string text = GetSelectedText();
    if (text.empty()) {
        text = buffer_;
    }
    Clipboard->SetString(text);
}

void UGuiTextInput::CutSelectionToClipboard()
{
    CopySelectionToClipboard();
    if (HasSelection()) {
        DeleteSelection();
        NotifyEdited();
    }
}

void UGuiTextInput::PasteFromClipboard()
{
    if (!Clipboard) {
        return;
    }
    InsertText(SanitizeConsolePaste(Clipboard->GetString(), buffer_.size(), kMaxLength));
}

bool UGuiTextInput::HandleEditShortcut(const GuiKeyEvent& event)
{
    if (!focused_ || !enabled_) {
        return false;
    }
    if (event.action != GuiKeyAction::Press && event.action != GuiKeyAction::Repeat) {
        return false;
    }
    if ((event.mods & GLFW_MOD_CONTROL) == 0) {
        return false;
    }
    if (event.keyCode == GLFW_KEY_A) {
        SelectAll();
        return true;
    }
    if (event.keyCode == GLFW_KEY_C) {
        CopySelectionToClipboard();
        return true;
    }
    if (event.keyCode == GLFW_KEY_X) {
        CutSelectionToClipboard();
        return true;
    }
    if (event.keyCode == GLFW_KEY_V) {
        PasteFromClipboard();
        return true;
    }
    return false;
}

void UGuiTextInput::Draw(UGuiRenderer& renderer)
{
    if (!visible_ || !theme_) {
        return;
    }
    renderer.DrawFilledRect(bounds_, theme_->buttonNormal);
    if (HasSelection()) {
        const int left = TextLeft();
        const int top = bounds_.y + TextPadding();
        const int h = theme_->fontSizeBody + 2;
        const int x0 = left + renderer.MeasureTextWidth(buffer_.substr(0, SelMin()));
        const int x1 = left + renderer.MeasureTextWidth(buffer_.substr(0, SelMax()));
        renderer.DrawFilledRect({x0, top, std::max(1, x1 - x0), h}, theme_->slotSelectedFill);
    }
    renderer.DrawBorderRect(bounds_, focused_ ? theme_->slotSelected : theme_->panelBorder,
                            theme_->borderThickness);
    renderer.DrawText(buffer_, TextLeft(), bounds_.y + TextPadding(), theme_->textPrimary);
    if (focused_ && !HasSelection()) {
        const int cx = TextLeft() + renderer.MeasureTextWidth(buffer_.substr(0, caretPos_));
        const int top = bounds_.y + TextPadding();
        const glm::vec4 caretColor(theme_->textPrimary, 1.0f);
        renderer.DrawFilledRect({cx, top, 2, theme_->fontSizeBody + 2}, caretColor);
    }
    if (HasFocusHighlight()) {
        DrawWidgetFocusRing(renderer, *theme_, bounds_);
    }
}

bool UGuiTextInput::PointerDown(const GuiMouseEvent& event, UGuiRenderer& renderer)
{
    if (!visible_ || event.button != GuiMouseButton::Left || !bounds_.Contains(event.x, event.y)) {
        return false;
    }
    focused_ = true;
    draggingSelection_ = true;
    caretPos_ = CaretIndexFromX(event.x, renderer);
    selAnchor_ = selEnd_ = caretPos_;
    return true;
}

bool UGuiTextInput::PointerMove(const GuiMouseEvent& event, UGuiRenderer& renderer)
{
    if (!draggingSelection_ || !focused_) {
        return false;
    }
    caretPos_ = CaretIndexFromX(event.x, renderer);
    selEnd_ = caretPos_;
    return true;
}

bool UGuiTextInput::OnMouseDown(const GuiMouseEvent& event)
{
    if (!visible_) {
        return false;
    }
    if (event.button == GuiMouseButton::Right) {
        return bounds_.Contains(event.x, event.y);
    }
    return event.button == GuiMouseButton::Left && bounds_.Contains(event.x, event.y);
}

bool UGuiTextInput::OnMouseUp(const GuiMouseEvent& event)
{
    (void)event;
    draggingSelection_ = false;
    return focused_;
}

bool UGuiTextInput::OnMouseMove(const GuiMouseEvent& event)
{
    if (!draggingSelection_ || !focused_) {
        return false;
    }
    return bounds_.Contains(event.x, event.y) || draggingSelection_;
}

bool UGuiTextInput::OnChar(const GuiCharEvent& event)
{
    if (!focused_ || !enabled_) {
        return false;
    }
    if (suppressCharCodepoint_ != 0 && event.codepoint == suppressCharCodepoint_) {
        suppressCharCodepoint_ = 0;
        return true;
    }
    if (event.codepoint >= 32 && event.codepoint < 127) {
        if (buffer_.size() >= kMaxLength) {
            return true;
        }
        DeleteSelection();
        buffer_.insert(caretPos_, 1, static_cast<char>(event.codepoint));
        ++caretPos_;
        NotifyEdited();
        return true;
    }
    return false;
}

bool UGuiTextInput::OnKey(const GuiKeyEvent& event)
{
    if (!focused_ || !enabled_) {
        return false;
    }
    if (HandleEditShortcut(event)) {
        return true;
    }
    if (event.action != GuiKeyAction::Press && event.action != GuiKeyAction::Repeat) {
        return false;
    }
    const bool shift = (event.mods & GLFW_MOD_SHIFT) != 0;

    if (event.keyCode == GLFW_KEY_DELETE) {
        if (HasSelection()) {
            DeleteSelection();
        } else if (caretPos_ < buffer_.size()) {
            buffer_.erase(caretPos_, 1);
        }
        NotifyEdited();
        return true;
    }
    if (event.keyCode == GLFW_KEY_BACKSPACE) {
        if (HasSelection()) {
            DeleteSelection();
        } else if (caretPos_ > 0) {
            buffer_.erase(caretPos_ - 1, 1);
            --caretPos_;
        }
        NotifyEdited();
        return true;
    }
    if (event.keyCode == GLFW_KEY_LEFT) {
        if (caretPos_ > 0) {
            --caretPos_;
        }
        if (shift) {
            selEnd_ = caretPos_;
        } else {
            ClearSelection();
        }
        return true;
    }
    if (event.keyCode == GLFW_KEY_RIGHT) {
        if (caretPos_ < buffer_.size()) {
            ++caretPos_;
        }
        if (shift) {
            selEnd_ = caretPos_;
        } else {
            ClearSelection();
        }
        return true;
    }
    if (event.keyCode == GLFW_KEY_HOME) {
        caretPos_ = 0;
        if (shift) {
            selEnd_ = 0;
        } else {
            ClearSelection();
        }
        return true;
    }
    if (event.keyCode == GLFW_KEY_END) {
        caretPos_ = buffer_.size();
        if (shift) {
            selEnd_ = caretPos_;
        } else {
            ClearSelection();
        }
        return true;
    }
    if (event.keyCode >= GLFW_KEY_A && event.keyCode <= GLFW_KEY_Z) {
        char c = static_cast<char>('a' + (event.keyCode - GLFW_KEY_A));
        if ((event.mods & GLFW_MOD_SHIFT) != 0) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        suppressCharCodepoint_ = static_cast<unsigned int>(c);
        InsertText(std::string(1, c));
        return true;
    }
    if (event.keyCode >= GLFW_KEY_0 && event.keyCode <= GLFW_KEY_9) {
        const char c = static_cast<char>('0' + (event.keyCode - GLFW_KEY_0));
        suppressCharCodepoint_ = static_cast<unsigned int>(c);
        InsertText(std::string(1, c));
        return true;
    }
    if (event.keyCode == GLFW_KEY_SPACE) {
        suppressCharCodepoint_ = static_cast<unsigned int>(' ');
        InsertText(" ");
        return true;
    }
    return false;
}

} // namespace cutum
