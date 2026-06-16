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
  return event.KeyCode == GuiKey::Tab && event.Action == GuiKeyAction::Press;
}

bool IsActivationKey(const GuiKeyEvent &event)
{
  return event.Action == GuiKeyAction::Press &&
         (event.KeyCode == GuiKey::Enter || event.KeyCode == GuiKey::KpEnter ||
          event.KeyCode == GuiKey::Space);
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
  Root = root;
  ReleaseFocusWithoutNotify();
}

void UGuiInputRouter::ReleaseFocusWithoutNotify()
{
  KeyboardFocus = nullptr;
  MousePressedWidget = nullptr;
  HoveredWidget = nullptr;
  CaptureMouse = false;
  ModalKeyboard = false;
  FocusOrder.clear();
}

void UGuiInputRouter::SetActiveScreen(UGuiScreenBase *screen)
{
  Screen = screen;
}

void UGuiInputRouter::ClearInteractionState()
{
  SetKeyboardFocus(nullptr, false);
  MousePressedWidget = nullptr;
  HoveredWidget = nullptr;
  CaptureMouse = false;
  ModalKeyboard = false;
}

void UGuiInputRouter::SetKeyboardFocus(UGuiWidget *widget, bool reveal)
{
  if (KeyboardFocus != nullptr && KeyboardFocus != widget)
  {
    KeyboardFocus->SetFocusHighlight(false);
    if (auto *text = dynamic_cast<UGuiTextInput *>(KeyboardFocus))
    {
      text->SetFocused(false);
#ifdef __ANDROID__
      AndroidSoftKeyboardClearTarget();
#endif
    }
  }
  KeyboardFocus = widget;
  if (KeyboardFocus)
  {
    KeyboardFocus->SetFocusHighlight(true);
    if (auto *text = dynamic_cast<UGuiTextInput *>(KeyboardFocus))
    {
      text->SetFocused(true);
#ifdef __ANDROID__
      AndroidSoftKeyboardSetTarget(text);
#endif
    }
    if (reveal)
    {
      RevealWidgetForKeyboardFocus(Root, KeyboardFocus);
    }
    ModalKeyboard = dynamic_cast<UGuiTextInput *>(KeyboardFocus) != nullptr;
  }
  else
  {
    ModalKeyboard = false;
  }
}

void UGuiInputRouter::CollectFocusOrder()
{
  FocusOrder.clear();
  if (Root)
  {
    Root->CollectFocusables(FocusOrder);
  }
}

void UGuiInputRouter::FocusNext(bool reverse)
{
  CollectFocusOrder();
  if (FocusOrder.empty())
  {
    SetKeyboardFocus(nullptr, false);
    return;
  }
  if (!KeyboardFocus)
  {
    SetKeyboardFocus(reverse ? FocusOrder.back() : FocusOrder.front(), true);
    return;
  }
  auto it = std::find(FocusOrder.begin(), FocusOrder.end(), KeyboardFocus);
  if (it == FocusOrder.end())
  {
    SetKeyboardFocus(FocusOrder.front(), true);
    return;
  }
  if (reverse)
  {
    if (it == FocusOrder.begin())
    {
      SetKeyboardFocus(FocusOrder.back(), true);
    }
    else
    {
      SetKeyboardFocus(*(--it), true);
    }
  }
  else if (it + 1 == FocusOrder.end())
  {
    SetKeyboardFocus(FocusOrder.front(), true);
  }
  else
  {
    SetKeyboardFocus(*(it + 1), true);
  }
}

bool UGuiInputRouter::OnMouseDown(const GuiMouseEvent &event)
{
  if (!Root)
  {
    return false;
  }
  if (UGuiScrollView *scroll = FindDeepestScrollView(Root, event.X, event.Y))
  {
    if (scroll->BeginDeferredTouch(event))
    {
      CaptureMouse = true;
      MousePressedWidget = scroll;
      return true;
    }
  }
  UGuiWidget *hit = Root->HitTest(event.X, event.Y);
  UGuiWidget *focusHit = Root->HitTestFocusable(event.X, event.Y);
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
      CaptureMouse = true;
      MousePressedWidget = hit;
      return true;
    }
    return false;
  }
  CaptureMouse = false;
  MousePressedWidget = nullptr;
  return false;
}

bool UGuiInputRouter::OnMouseUp(const GuiMouseEvent &event)
{
  bool consumed = false;
  if (Root)
  {
    if (auto *scroll = dynamic_cast<UGuiScrollView *>(MousePressedWidget))
    {
      consumed = scroll->OnDeferredUp(event);
    }
    else if (MousePressedWidget)
    {
      consumed = MousePressedWidget->OnMouseUp(event);
    }
    else if (UGuiWidget *hit = Root->HitTest(event.X, event.Y))
    {
      consumed = hit->OnMouseUp(event);
    }
  }
  CaptureMouse = false;
  MousePressedWidget = nullptr;
  return consumed;
}

bool UGuiInputRouter::OnMouseMove(const GuiMouseEvent &event)
{
  LastMouseX = event.X;
  LastMouseY = event.Y;
  if (CaptureMouse && MousePressedWidget)
  {
    if (auto *scroll = dynamic_cast<UGuiScrollView *>(MousePressedWidget))
    {
      return scroll->OnDeferredMove(event);
    }
    return MousePressedWidget->OnMouseMove(event);
  }
  if (!Root)
  {
    return false;
  }
  UGuiWidget *hit = Root->HitTest(event.X, event.Y);
  HoveredWidget = hit;
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
    const bool reverse = (event.Mods & GLFW_MOD_SHIFT) != 0;
    FocusNext(reverse);
    return true;
  }

  if (KeyboardFocus && IsActivationKey(event))
  {
    if (auto *text = dynamic_cast<UGuiTextInput *>(KeyboardFocus))
    {
      if (event.KeyCode == GuiKey::Space)
      {
        return text->OnKey(event);
      }
    }
    else if (KeyboardFocus->Activate())
    {
      return true;
    }
  }

  if (KeyboardFocus && KeyboardFocus->OnKey(event))
  {
    return true;
  }
  if (Root)
  {
    return Root->OnKey(event);
  }
  return false;
}

bool UGuiInputRouter::OnChar(const GuiCharEvent &event)
{
  if (KeyboardFocus && KeyboardFocus->OnChar(event))
  {
    return true;
  }
  if (Root)
  {
    return Root->OnChar(event);
  }
  return false;
}

bool UGuiInputRouter::OnScroll(const GuiScrollEvent &event, int mouseX,
                               int mouseY)
{
  if (!Root)
  {
    return false;
  }
  if (mouseX >= 0 && mouseY >= 0)
  {
    return Root->ScrollAtPoint(mouseX, mouseY, event);
  }
  if (LastMouseX >= 0 && LastMouseY >= 0)
  {
    return Root->ScrollAtPoint(LastMouseX, LastMouseY, event);
  }
  return Root->OnScroll(event);
}

bool UGuiInputRouter::WantsCaptureMouse() const
{
  return CaptureMouse || ModalKeyboard || (Screen && Screen->BlocksGameInput());
}

bool UGuiInputRouter::WantsCaptureKeyboard() const
{
  return ModalKeyboard || KeyboardFocus != nullptr ||
         (Screen && Screen->BlocksGameInput());
}

} // namespace cutum
