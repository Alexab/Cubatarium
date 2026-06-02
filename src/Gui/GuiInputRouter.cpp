#include "GuiInputRouter.h"
#include "Gui/GuiFocus.h"
#include "Gui/GuiScreenBase.h"
#include "Gui/Widgets/GuiWidget.h"
#include "Gui/Widgets/GuiTextInput.h"

#include <GLFW/glfw3.h>
#include <algorithm>

namespace cutum {

namespace {

bool IsTabKey(const GuiKeyEvent& event)
{
    return event.keyCode == GLFW_KEY_TAB && event.action == GuiKeyAction::Press;
}

bool IsActivationKey(const GuiKeyEvent& event)
{
    return event.action == GuiKeyAction::Press &&
           (event.keyCode == GLFW_KEY_ENTER || event.keyCode == GLFW_KEY_KP_ENTER ||
            event.keyCode == GLFW_KEY_SPACE);
}

} // namespace

void GuiInputRouter::SetRoot(GuiWidget* root)
{
    root_ = root;
    ReleaseFocusWithoutNotify();
}

void GuiInputRouter::ReleaseFocusWithoutNotify()
{
    keyboardFocus_ = nullptr;
    mousePressedWidget_ = nullptr;
    hoveredWidget_ = nullptr;
    captureMouse_ = false;
    modalKeyboard_ = false;
    focusOrder_.clear();
}

void GuiInputRouter::SetActiveScreen(GuiScreenBase* screen)
{
    screen_ = screen;
}

void GuiInputRouter::ClearInteractionState()
{
    SetKeyboardFocus(nullptr, false);
    mousePressedWidget_ = nullptr;
    hoveredWidget_ = nullptr;
    captureMouse_ = false;
    modalKeyboard_ = false;
}

void GuiInputRouter::SetKeyboardFocus(GuiWidget* widget, bool reveal)
{
    if (keyboardFocus_ != nullptr && keyboardFocus_ != widget) {
        keyboardFocus_->SetFocusHighlight(false);
        if (auto* text = dynamic_cast<GuiTextInput*>(keyboardFocus_)) {
            text->SetFocused(false);
        }
    }
    keyboardFocus_ = widget;
    if (keyboardFocus_) {
        keyboardFocus_->SetFocusHighlight(true);
        if (auto* text = dynamic_cast<GuiTextInput*>(keyboardFocus_)) {
            text->SetFocused(true);
        }
        if (reveal) {
            RevealWidgetForKeyboardFocus(root_, keyboardFocus_);
        }
        modalKeyboard_ = dynamic_cast<GuiTextInput*>(keyboardFocus_) != nullptr;
    } else {
        modalKeyboard_ = false;
    }
}

void GuiInputRouter::CollectFocusOrder()
{
    focusOrder_.clear();
    if (root_) {
        root_->CollectFocusables(focusOrder_);
    }
}

void GuiInputRouter::FocusNext(bool reverse)
{
    CollectFocusOrder();
    if (focusOrder_.empty()) {
        SetKeyboardFocus(nullptr, false);
        return;
    }
    if (!keyboardFocus_) {
        SetKeyboardFocus(reverse ? focusOrder_.back() : focusOrder_.front(), true);
        return;
    }
    auto it = std::find(focusOrder_.begin(), focusOrder_.end(), keyboardFocus_);
    if (it == focusOrder_.end()) {
        SetKeyboardFocus(focusOrder_.front(), true);
        return;
    }
    if (reverse) {
        if (it == focusOrder_.begin()) {
            SetKeyboardFocus(focusOrder_.back(), true);
        } else {
            SetKeyboardFocus(*(--it), true);
        }
    } else if (it + 1 == focusOrder_.end()) {
        SetKeyboardFocus(focusOrder_.front(), true);
    } else {
        SetKeyboardFocus(*(it + 1), true);
    }
}

bool GuiInputRouter::OnMouseDown(const GuiMouseEvent& event)
{
    if (!root_) {
        return false;
    }
    GuiWidget* hit = root_->HitTest(event.x, event.y);
    GuiWidget* focusHit = root_->HitTestFocusable(event.x, event.y);
    if (focusHit) {
        SetKeyboardFocus(focusHit, false);
    } else if (!hit) {
        SetKeyboardFocus(nullptr, false);
    }
    if (hit) {
        captureMouse_ = true;
        mousePressedWidget_ = hit;
        return hit->OnMouseDown(event);
    }
    captureMouse_ = false;
    mousePressedWidget_ = nullptr;
    return false;
}

bool GuiInputRouter::OnMouseUp(const GuiMouseEvent& event)
{
    bool consumed = false;
    if (root_) {
        if (GuiWidget* hit = root_->HitTest(event.x, event.y)) {
            consumed = hit->OnMouseUp(event);
        } else if (mousePressedWidget_) {
            consumed = mousePressedWidget_->OnMouseUp(event);
        }
    }
    captureMouse_ = false;
    mousePressedWidget_ = nullptr;
    return consumed;
}

bool GuiInputRouter::OnMouseMove(const GuiMouseEvent& event)
{
    lastMouseX_ = event.x;
    lastMouseY_ = event.y;
    if (!root_) {
        return false;
    }
    GuiWidget* hit = root_->HitTest(event.x, event.y);
    hoveredWidget_ = hit;
    if (hit) {
        return hit->OnMouseMove(event);
    }
    return false;
}

bool GuiInputRouter::OnKey(const GuiKeyEvent& event)
{
    if (IsTabKey(event)) {
        const bool reverse = (event.mods & GLFW_MOD_SHIFT) != 0;
        FocusNext(reverse);
        return true;
    }

    if (keyboardFocus_ && IsActivationKey(event)) {
        if (auto* text = dynamic_cast<GuiTextInput*>(keyboardFocus_)) {
            if (event.keyCode == GLFW_KEY_SPACE) {
                return text->OnKey(event);
            }
        } else if (keyboardFocus_->Activate()) {
            return true;
        }
    }

    if (keyboardFocus_ && keyboardFocus_->OnKey(event)) {
        return true;
    }
    if (root_) {
        return root_->OnKey(event);
    }
    return false;
}

bool GuiInputRouter::OnChar(const GuiCharEvent& event)
{
    if (keyboardFocus_ && keyboardFocus_->OnChar(event)) {
        return true;
    }
    if (root_) {
        return root_->OnChar(event);
    }
    return false;
}

bool GuiInputRouter::OnScroll(const GuiScrollEvent& event, int mouseX, int mouseY)
{
    if (!root_) {
        return false;
    }
    if (mouseX >= 0 && mouseY >= 0) {
        return root_->ScrollAtPoint(mouseX, mouseY, event);
    }
    if (lastMouseX_ >= 0 && lastMouseY_ >= 0) {
        return root_->ScrollAtPoint(lastMouseX_, lastMouseY_, event);
    }
    return root_->OnScroll(event);
}

bool GuiInputRouter::WantsCaptureMouse() const
{
    return captureMouse_ || modalKeyboard_ || (screen_ && screen_->BlocksGameInput());
}

bool GuiInputRouter::WantsCaptureKeyboard() const
{
    return modalKeyboard_ || keyboardFocus_ != nullptr || (screen_ && screen_->BlocksGameInput());
}

} // namespace cutum
