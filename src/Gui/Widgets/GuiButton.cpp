#include "Gui/Widgets/GuiButton.h"
#include "Gui/Core/GuiFocus.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Core/GuiTheme.h"
#include "Gui/Core/GuiTypes.h"

namespace cutum
{

UGuiButton::UGuiButton(const GuiTheme *theme, std::string label)
    : Theme(theme), Label(std::move(label))
{
}

int UGuiButton::GetPreferredHeight() const
{
  return Theme ? Theme->FontSizeBody + Theme->Padding * 2 : 32;
}

bool UGuiButton::CanFocus() const { return Enabled && Visible; }

bool UGuiButton::Activate()
{
  if (!CanFocus() || !OnClick)
  {
    return false;
  }
  OnClick();
  return true;
}

glm::vec4 UGuiButton::StateColor() const
{
  if (!Theme)
  {
    return glm::vec4(0.3f);
  }
  if (!Enabled)
  {
    return Theme->ButtonDisabled;
  }
  switch (State)
  {
  case GuiButtonState::Hovered:
    return Theme->ButtonHover;
  case GuiButtonState::Pressed:
    return Theme->ButtonPressed;
  default:
    return Theme->ButtonNormal;
  }
}

void UGuiButton::Draw(UGuiRenderer &renderer)
{
  if (!Visible || !Theme)
  {
    return;
  }
  renderer.DrawFilledRect(Bounds, StateColor());
  renderer.DrawBorderRect(Bounds, Theme->PanelBorder, Theme->BorderThickness);
  GuiRect textRect = Bounds;
  if (Label.size() == 1)
  {
    if (Label[0] == '+')
    {
      textRect.Y += 2;
    }
    else if (Label[0] == '-')
    {
      textRect.Y += 3;
    }
  }
  renderer.DrawTextCenteredInRect(textRect, Label, Theme->TextPrimary);
  if (HasFocusHighlight())
  {
    DrawWidgetFocusRing(renderer, *Theme, Bounds);
  }
}

bool UGuiButton::OnMouseDown(const GuiMouseEvent &event)
{
  if (!Enabled || !Visible || !Bounds.Contains(event.X, event.Y))
  {
    return false;
  }
  if (event.Button != GuiMouseButton::Left)
  {
    return false;
  }
  State = GuiButtonState::Pressed;
  PressedInside = true;
  DownX = event.X;
  DownY = event.Y;
  Dragged = false;
  return true;
}

bool UGuiButton::OnMouseUp(const GuiMouseEvent &event)
{
  if (!Enabled || !Visible)
  {
    return false;
  }
  const bool inside = Bounds.Contains(event.X, event.Y);
  const bool wasPressed = PressedInside;
  if (wasPressed && event.Button == GuiMouseButton::Left && OnClick && !Dragged)
  {
    OnClick();
  }
  PressedInside = false;
  Dragged = false;
  State = inside ? GuiButtonState::Hovered : GuiButtonState::Normal;
  return wasPressed || inside;
}

bool UGuiButton::OnMouseMove(const GuiMouseEvent &event)
{
  if (!Visible)
  {
    return false;
  }
  const bool inside = Bounds.Contains(event.X, event.Y);
  if (!Enabled)
  {
    State = GuiButtonState::Disabled;
    return inside;
  }
  if (State != GuiButtonState::Pressed)
  {
    State = inside ? GuiButtonState::Hovered : GuiButtonState::Normal;
  }
  else if (PressedInside && !Dragged)
  {
    const int dx = event.X - DownX;
    const int dy = event.Y - DownY;
    if (dx * dx + dy * dy > kGuiTouchDragSlopPx * kGuiTouchDragSlopPx)
    {
      Dragged = true;
    }
  }
  return inside;
}

} // namespace cutum
