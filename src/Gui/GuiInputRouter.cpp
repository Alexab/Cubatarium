#include "GuiInputRouter.h"
#include "Gui/GuiScreenBase.h"
#include "Gui/Widgets/GuiWidget.h"
#include "Gui/Widgets/GuiTextInput.h"

namespace cutum {

void GuiInputRouter::SetRoot(GuiWidget* root)
{
    root_ = root;
}

void GuiInputRouter::SetActiveScreen(GuiScreenBase* screen)
{
    screen_ = screen;
}

bool GuiInputRouter::OnMouseDown(const GuiMouseEvent& event)
{
    if (!root_) {
        return false;
    }
    GuiWidget* hit = root_->HitTest(event.x, event.y);
    if (hit) {
        captureMouse_ = true;
        focusedWidget_ = hit;
        modalKeyboard_ = dynamic_cast<GuiTextInput*>(hit) != nullptr;
        return hit->OnMouseDown(event);
    }
    captureMouse_ = false;
    focusedWidget_ = nullptr;
    modalKeyboard_ = false;
    return false;
}

bool GuiInputRouter::OnMouseUp(const GuiMouseEvent& event)
{
    bool consumed = false;
    if (root_) {
        if (GuiWidget* hit = root_->HitTest(event.x, event.y)) {
            consumed = hit->OnMouseUp(event);
        } else if (focusedWidget_) {
            consumed = focusedWidget_->OnMouseUp(event);
        }
    }
    captureMouse_ = false;
    return consumed;
}

bool GuiInputRouter::OnMouseMove(const GuiMouseEvent& event)
{
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
    if (focusedWidget_ && focusedWidget_->OnKey(event)) {
        return true;
    }
    if (root_) {
        return root_->OnKey(event);
    }
    return false;
}

bool GuiInputRouter::OnChar(const GuiCharEvent& event)
{
    if (focusedWidget_ && focusedWidget_->OnChar(event)) {
        return true;
    }
    if (root_) {
        return root_->OnChar(event);
    }
    return false;
}

bool GuiInputRouter::OnScroll(const GuiScrollEvent& event)
{
    if (!root_) {
        return false;
    }
    return root_->OnScroll(event);
}

bool GuiInputRouter::WantsCaptureMouse() const
{
    return captureMouse_ || modalKeyboard_ || (screen_ && screen_->BlocksGameInput());
}

bool GuiInputRouter::WantsCaptureKeyboard() const
{
    return modalKeyboard_ || (screen_ && screen_->BlocksGameInput());
}

} // namespace cutum
