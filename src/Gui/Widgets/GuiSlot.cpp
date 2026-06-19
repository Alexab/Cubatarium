#include "Gui/Widgets/GuiSlot.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Core/GuiTheme.h"

#include <cmath>

namespace cutum
{

namespace
{
constexpr int kDragThresholdPx = 8;
}

UGuiSlot::UGuiSlot(const GuiTheme *theme, int size)
    : Theme(theme), SlotSize(size)
{
  Bounds.W = size;
  Bounds.H = size;
}

int UGuiSlot::GetPreferredWidth() const { return SlotSize; }
int UGuiSlot::GetPreferredHeight() const { return SlotSize; }

void UGuiSlot::Draw(UGuiRenderer &renderer)
{
  if (!Visible || !Theme)
  {
    return;
  }
  renderer.DrawFilledRect(Bounds, Theme->SlotBackground);
  if (Selected)
  {
    renderer.DrawFilledRect(Bounds, Theme->SlotSelectedFill);
  }

  if (IconTexture != 0)
  {
    const int inset = 4;
    const GuiRect iconRect{Bounds.X + inset, Bounds.Y + inset,
                           Bounds.W - inset * 2, Bounds.H - inset * 2};
    renderer.DrawTexturedRect(iconRect, IconTexture);
  }
  else if (!Label.empty())
  {
    std::string text = Label;
    if (text.size() > 8)
    {
      text = text.substr(0, 8);
    }
    renderer.DrawText(text, Bounds.X + 3, Bounds.Y + Bounds.H / 2 - 6,
                      Theme->TextSecondary);
  }

  if (Selected)
  {
    renderer.DrawBorderRect(Bounds, Theme->SlotSelected,
                            Theme->SlotSelectedBorderThickness);
    const GuiRect inner = Bounds.Inset(2);
    renderer.DrawBorderRect(inner, Theme->SlotSelectedInner, 1);
  }
  else
  {
    renderer.DrawBorderRect(Bounds, Theme->PanelBorder, Theme->BorderThickness);
  }

  if (!CornerHint.empty())
  {
    const glm::vec3 textColor =
        Selected ? glm::vec3(0.95f, 0.95f, 0.95f) : Theme->TextSecondary;
    renderer.DrawText(CornerHint, Bounds.X + 4, Bounds.Y + 2, textColor);
  }

  if (Dimmed)
  {
    renderer.DrawFilledRect(Bounds, Theme->SlotDisabledFill);
  }
}

bool UGuiSlot::OnMouseDown(const GuiMouseEvent &event)
{
  if (!Enabled || !Visible || !Bounds.Contains(event.X, event.Y))
  {
    return false;
  }
  Pressed = true;
  DragStarted = false;
  PressX = event.X;
  PressY = event.Y;
  return true;
}

bool UGuiSlot::OnMouseUp(const GuiMouseEvent &event)
{
  if (!Enabled || !Pressed)
  {
    return false;
  }
  const int dx = event.X - PressX;
  const int dy = event.Y - PressY;
  const bool isClickGesture =
      (dx * dx + dy * dy) <= (kDragThresholdPx * kDragThresholdPx);
  if (isClickGesture && OnClick)
  {
    OnClick();
  }
  Pressed = false;
  DragStarted = false;
  return true;
}

bool UGuiSlot::OnMouseMove(const GuiMouseEvent &event)
{
  if (!Pressed || DragStarted)
  {
    return Pressed;
  }
  const int dx = event.X - PressX;
  const int dy = event.Y - PressY;
  if ((dx * dx + dy * dy) > (kDragThresholdPx * kDragThresholdPx))
  {
    DragStarted = true;
    if (OnBeginDrag)
    {
      OnBeginDrag();
    }
  }
  return true;
}

} // namespace cutum
