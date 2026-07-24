#include "Gui/Widgets/GuiSlider.h"

#include "Gui/Core/GuiRenderer.h"
#include "Gui/Core/GuiTheme.h"
#include "Gui/Core/GuiTypes.h"

#include <algorithm>
#include <cmath>

namespace cutum
{

UGuiSlider::UGuiSlider(const GuiTheme *theme) : Theme(theme) {}

void UGuiSlider::SetRange(float min, float max)
{
  Min = min;
  Max = std::max(min, max);
  ApplyValue(Value, false);
}

void UGuiSlider::SetValue(float value) { ApplyValue(value, false); }

int UGuiSlider::GetPreferredHeight() const
{
  return Theme ? Theme->ProgressBarHeight + Theme->Padding : 24;
}

GuiRect UGuiSlider::TrackRect() const
{
  const int barH = Theme ? Theme->ProgressBarHeight : 14;
  const int y = Bounds.Y + (Bounds.H - barH) / 2;
  return {Bounds.X, y, Bounds.W, barH};
}

GuiRect UGuiSlider::ThumbRect() const
{
  const GuiRect track = TrackRect();
  if (track.W <= 0)
  {
    return track;
  }
  const int thumbW = std::max(12, track.H + 4);
  const float t = (Max > Min) ? (Value - Min) / (Max - Min) : 0.f;
  const int travel = std::max(1, track.W - thumbW);
  const int thumbX = track.X + static_cast<int>(std::round(t * travel));
  return {thumbX, track.Y - 2, thumbW, track.H + 4};
}

float UGuiSlider::ValueFromX(int x) const
{
  const GuiRect track = TrackRect();
  const int thumbW = std::max(12, track.H + 4);
  const int travel = std::max(1, track.W - thumbW);
  const float t =
      static_cast<float>(x - track.X - thumbW / 2) / static_cast<float>(travel);
  return Min + std::clamp(t, 0.f, 1.f) * (Max - Min);
}

void UGuiSlider::ApplyValue(float value, bool notify)
{
  const float before = Value;
  Value = std::clamp(value, Min, Max);
  if (Step > 0.f)
  {
    Value = Min + std::round((Value - Min) / Step) * Step;
    Value = std::clamp(Value, Min, Max);
  }
  if (notify && OnValueChanged && std::fabs(Value - before) > 1e-6f)
  {
    OnValueChanged(Value);
  }
}

void UGuiSlider::Draw(UGuiRenderer &renderer)
{
  if (!Visible || !Theme)
  {
    return;
  }

  const GuiRect track = TrackRect();
  renderer.DrawFilledRect(track, Theme->ProgressTrack);
  renderer.DrawBorderRect(track, Theme->PanelBorder, Theme->BorderThickness);

  const float t = (Max > Min) ? (Value - Min) / (Max - Min) : 0.f;
  const int fillW = static_cast<int>(std::round(track.W * t));
  if (fillW > 0)
  {
    renderer.DrawFilledRect({track.X, track.Y, fillW, track.H},
                            Theme->ProgressFill);
  }

  const GuiRect thumb = ThumbRect();
  renderer.DrawFilledRect(thumb, Dragging ? Theme->ButtonPressed : Theme->ButtonHover);
  renderer.DrawBorderRect(thumb, Theme->PanelBorder, Theme->BorderThickness);
}

bool UGuiSlider::ConsumesScrollDragAt(int x, int y) const
{
  return Visible && Enabled && Bounds.Contains(x, y);
}

bool UGuiSlider::OnMouseDown(const GuiMouseEvent &event)
{
  if (!Enabled || !Visible || event.Button != GuiMouseButton::Left ||
      !Bounds.Contains(event.X, event.Y))
  {
    return false;
  }
  Dragging = true;
  ApplyValue(ValueFromX(event.X), true);
  return true;
}

bool UGuiSlider::OnMouseUp(const GuiMouseEvent &event)
{
  if (!Dragging)
  {
    return false;
  }
  if (event.Button != GuiMouseButton::Left)
  {
    return false;
  }
  Dragging = false;
  ApplyValue(ValueFromX(event.X), true);
  if (OnCommit)
  {
    OnCommit(Value);
  }
  return true;
}

bool UGuiSlider::OnMouseMove(const GuiMouseEvent &event)
{
  if (!Dragging)
  {
    return false;
  }
  ApplyValue(ValueFromX(event.X), true);
  return true;
}

} // namespace cutum
