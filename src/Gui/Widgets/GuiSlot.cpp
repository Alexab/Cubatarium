#include "Gui/Widgets/GuiSlot.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Core/GuiTheme.h"

#include <cmath>

namespace cutum
{

UGuiSlot::UGuiSlot(const GuiTheme *theme) : Theme(theme)
{
  const int size = SlotSizePx();
  Bounds.W = size;
  Bounds.H = size;
}

int UGuiSlot::SlotSizePx() const
{
  return Theme ? Theme->HotbarSlotSize : 48;
}

int UGuiSlot::DragThresholdPx() const
{
  return Theme ? Theme->SlotDragThresholdPx : 8;
}

int UGuiSlot::GetPreferredWidth() const { return SlotSizePx(); }
int UGuiSlot::GetPreferredHeight() const { return SlotSizePx(); }

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
    const int inset = Theme->SlotIconInset;
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
    renderer.DrawTextCenteredInRect(Bounds, text, Theme->TextSecondary);
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
    renderer.DrawText(CornerHint, Bounds.X + Theme->Padding / 2,
                      Bounds.Y + Theme->Padding / 4, textColor);
  }

  if (Dimmed || Broken)
  {
    // Broken tools: red tint overlay (C3 UX).
    const glm::vec4 tint =
        Broken ? glm::vec4(0.85f, 0.12f, 0.1f, 0.45f) : Theme->SlotDisabledFill;
    renderer.DrawFilledRect(Bounds, tint);
  }

  if (WearProgress > 0.01f && WearProgress < 1.f)
  {
    const int barH = 3;
    const GuiRect track{Bounds.X + 2, Bounds.Y + Bounds.H - barH - 2,
                        Bounds.W - 4, barH};
    renderer.DrawFilledRect(track, glm::vec4(0.1f, 0.1f, 0.1f, 0.85f));
    const float remain = 1.f - WearProgress;
    const int fillW = static_cast<int>((Bounds.W - 4) * remain);
    glm::vec4 color{0.2f, 0.85f, 0.25f, 1.f};
    if (remain < 0.35f)
    {
      color = glm::vec4(0.9f, 0.2f, 0.15f, 1.f);
    }
    else if (remain < 0.65f)
    {
      color = glm::vec4(0.9f, 0.85f, 0.15f, 1.f);
    }
    renderer.DrawFilledRect({track.X, track.Y, fillW, barH}, color);
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
  const int threshold = DragThresholdPx();
  const bool isClickGesture =
      (dx * dx + dy * dy) <= (threshold * threshold);
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
  const int threshold = DragThresholdPx();
  if ((dx * dx + dy * dy) > (threshold * threshold))
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
