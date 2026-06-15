#include "Gui/Core/GuiInputRouter.h"
#include "Gui/Core/GuiFocus.h"
#include "Gui/Core/GuiScreenBase.h"
#include "Gui/Widgets/GuiScrollView.h"
#include "Gui/Widgets/GuiTextInput.h"
#include "Gui/Widgets/GuiWidget.h"

#include "Gui/Core/GuiKeyCodes.h"
#include <algorithm>
#ifdef __ANDROID__
#include "android_soft_keyboard.h"
#endif

namespace cutum
{

namespace
{

bool IsTabKey(const GuiKeyEvent &event)
{
  return event.keyCode == GuiKey::Tab && event.action == GuiKeyAction::Press;
}

bool IsActivationKey(const GuiKeyEvent &event)
{
  return event.action == GuiKeyAction::Press &&
         (event.keyCode == GuiKey::Enter ||
          event.keyCode == GuiKey::KpEnter ||
          event.keyCode == GuiKey::Space);
}

UGuiScrollView *FindDeepestScrollView(UGuiWidget *node, int x, int y)
{
  if (!node || !node->IsVisible() || !node->GetBounds().Contains(x, y))
  {
    return nullptr;
  }
  for (const auto &child : node->GetChildren())
  {
    if (UGuiScrollView *hit = FindDeepestScrollView(child.get(), x, y))
    {
      return hit;
    }
  }
  if (auto *scroll = dynamic_cast<UGuiScrollView *>(node))
  {
    if (scroll->MaxScrollY() > 0)
    {
      return scroll;
    }
  }
  return nullptr;
}

} // namespace

void UGuiInputRouter::SetRoot(UGuiWidget *root)
{
  root_ = root;
  ReleaseFocusWithoutNotify();
}

void UGuiInputRouter::ReleaseFocusWithoutNotify()
{
  keyboardFocus_ = nullptr;
  mousePressedWidget_ = nullptr;
  hoveredWidget_ = nullptr;
  captureMouse_ = false;
  modalKeyboard_ = false;
  focusOrder_.clear();
}

void UGuiInputRouter::SetActiveScreen(UGuiScreenBase *screen)
{
  screen_ = screen;
}

void UGuiInputRouter::ClearInteractionState()
{
  SetKeyboardFocus(nullptr, false);
  mousePressedWidget_ = nullptr;
  hoveredWidget_ = nullptr;
  captureMouse_ = false;
  modalKeyboard_ = false;
}

void UGuiInputRouter::SetKeyboardFocus(UGuiWidget *widget, bool reveal)
{
  if (keyboardFocus_ != nullptr && keyboardFocus_ != widget)
  {
    keyboardFocus_->SetFocusHighlight(false);
    if (auto *text = dynamic_cast<UGuiTextInput *>(keyboardFocus_))
    {
      text->SetFocused(false);
#ifdef __ANDROID__
      AndroidSoftKeyboardClearTarget();
#endif
    }
  }
  keyboardFocus_ = widget;
  if (keyboardFocus_)
  {
    keyboardFocus_->SetFocusHighlight(true);
    if (auto *text = dynamic_cast<UGuiTextInput *>(keyboardFocus_))
    {
      text->SetFocused(true);
#ifdef __ANDROID__
      AndroidSoftKeyboardSetTarget(text);
#endif
    }
    if (reveal)
    {
      RevealWidgetForKeyboardFocus(root_, keyboardFocus_);
    }
    modalKeyboard_ = dynamic_cast<UGuiTextInput *>(keyboardFocus_) != nullptr;
  }
  else
  {
    modalKeyboard_ = false;
  }
}

void UGuiInputRouter::CollectFocusOrder()
{
  focusOrder_.clear();
  if (root_)
  {
    root_->CollectFocusables(focusOrder_);
  }
}

void UGuiInputRouter::FocusNext(bool reverse)
{
  CollectFocusOrder();
  if (focusOrder_.empty())
  {
    SetKeyboardFocus(nullptr, false);
    return;
  }
  if (!keyboardFocus_)
  {
    SetKeyboardFocus(reverse ? focusOrder_.back() : focusOrder_.front(), true);
    return;
  }
  auto it = std::find(focusOrder_.begin(), focusOrder_.end(), keyboardFocus_);
  if (it == focusOrder_.end())
  {
    SetKeyboardFocus(focusOrder_.front(), true);
    return;
  }
  if (reverse)
  {
    if (it == focusOrder_.begin())
    {
      SetKeyboardFocus(focusOrder_.back(), true);
    }
    else
    {
      SetKeyboardFocus(*(--it), true);
    }
  }
  else if (it + 1 == focusOrder_.end())
  {
    SetKeyboardFocus(focusOrder_.front(), true);
  }
  else
  {
    SetKeyboardFocus(*(it + 1), true);
  }
}

bool UGuiInputRouter::OnMouseDown(const GuiMouseEvent &event)
{
  if (!root_)
  {
    return false;
  }
  if (UGuiScrollView *scroll =
          FindDeepestScrollView(root_, event.x, event.y))
  {
    if (scroll->BeginDeferredTouch(event))
    {
      captureMouse_ = true;
      mousePressedWidget_ = scroll;
      return true;
    }
  }
  UGuiWidget *hit = root_->HitTest(event.x, event.y);
  UGuiWidget *focusHit = root_->HitTestFocusable(event.x, event.y);
  if (focusHit)
  {
    SetKeyboardFocus(focusHit, false);
  }
  else if (!hit)
  {
    SetKeyboardFocus(nullptr, false);
  }
  if (hit)
  {
    if (hit->OnMouseDown(event))
    {
      captureMouse_ = true;
      mousePressedWidget_ = hit;
      return true;
    }
    return false;
  }
  captureMouse_ = false;
  mousePressedWidget_ = nullptr;
  return false;
}

bool UGuiInputRouter::OnMouseUp(const GuiMouseEvent &event)
{
  bool consumed = false;
  if (root_)
  {
    if (auto *scroll = dynamic_cast<UGuiScrollView *>(mousePressedWidget_))
    {
      consumed = scroll->OnDeferredUp(event);
    }
    else if (mousePressedWidget_)
    {
      consumed = mousePressedWidget_->OnMouseUp(event);
    }
    else if (UGuiWidget *hit = root_->HitTest(event.x, event.y))
    {
      consumed = hit->OnMouseUp(event);
    }
  }
  captureMouse_ = false;
  mousePressedWidget_ = nullptr;
  return consumed;
}

bool UGuiInputRouter::OnMouseMove(const GuiMouseEvent &event)
{
  lastMouseX_ = event.x;
  lastMouseY_ = event.y;
  if (captureMouse_ && mousePressedWidget_)
  {
    if (auto *scroll = dynamic_cast<UGuiScrollView *>(mousePressedWidget_))
    {
      return scroll->OnDeferredMove(event);
    }
    return mousePressedWidget_->OnMouseMove(event);
  }
  if (!root_)
  {
    return false;
  }
  UGuiWidget *hit = root_->HitTest(event.x, event.y);
  hoveredWidget_ = hit;
  if (hit)
  {
    return hit->OnMouseMove(event);
  }
  return false;
}

bool UGuiInputRouter::OnKey(const GuiKeyEvent &event)
{
  if (IsTabKey(event))
  {
    const bool reverse = (event.mods & GLFW_MOD_SHIFT) != 0;
    FocusNext(reverse);
    return true;
  }

  if (keyboardFocus_ && IsActivationKey(event))
  {
    if (auto *text = dynamic_cast<UGuiTextInput *>(keyboardFocus_))
    {
      if (event.keyCode == GuiKey::Space)
      {
        return text->OnKey(event);
      }
    }
    else if (keyboardFocus_->Activate())
    {
      return true;
    }
  }

  if (keyboardFocus_ && keyboardFocus_->OnKey(event))
  {
    return true;
  }
  if (root_)
  {
    return root_->OnKey(event);
  }
  return false;
}

bool UGuiInputRouter::OnChar(const GuiCharEvent &event)
{
  if (keyboardFocus_ && keyboardFocus_->OnChar(event))
  {
    return true;
  }
  if (root_)
  {
    return root_->OnChar(event);
  }
  return false;
}

bool UGuiInputRouter::OnScroll(const GuiScrollEvent &event, int mouseX,
                               int mouseY)
{
  if (!root_)
  {
    return false;
  }
  if (mouseX >= 0 && mouseY >= 0)
  {
    return root_->ScrollAtPoint(mouseX, mouseY, event);
  }
  if (lastMouseX_ >= 0 && lastMouseY_ >= 0)
  {
    return root_->ScrollAtPoint(lastMouseX_, lastMouseY_, event);
  }
  return root_->OnScroll(event);
}

bool UGuiInputRouter::WantsCaptureMouse() const
{
  return captureMouse_ || modalKeyboard_ ||
         (screen_ && screen_->BlocksGameInput());
}

bool UGuiInputRouter::WantsCaptureKeyboard() const
{
  return modalKeyboard_ || keyboardFocus_ != nullptr ||
         (screen_ && screen_->BlocksGameInput());
}

} // namespace cutum
