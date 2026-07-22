#include "Gui/Core/GuiScrollbarController.h"

#include <algorithm>

namespace cutum
{

namespace
{

bool ContainsRect(const GuiRect &rect, int x, int y)
{
  return rect.W > 0 && rect.H > 0 && rect.Contains(x, y);
}

} // namespace

int UGuiScrollbarController::ClampScroll(int scroll, int maxScroll)
{
  return std::clamp(scroll, 0, std::max(0, maxScroll));
}

GuiScrollbarHit UGuiScrollbarController::HitTest(const GuiScrollbarMetrics &metrics,
                                                 int x, int y) const
{
  if (!ContainsRect(metrics.Track, x, y))
  {
    return GuiScrollbarHit::None;
  }
  if (ContainsRect(metrics.Thumb, x, y))
  {
    return GuiScrollbarHit::Thumb;
  }
  if (y < metrics.Thumb.Y)
  {
    return GuiScrollbarHit::TrackAbove;
  }
  if (y >= metrics.Thumb.Y + metrics.Thumb.H)
  {
    return GuiScrollbarHit::TrackBelow;
  }
  return GuiScrollbarHit::Thumb;
}

bool UGuiScrollbarController::BeginThumbDrag(const GuiScrollbarMetrics &metrics,
                                             int mouseY)
{
  if (metrics.MaxScroll <= 0 || metrics.Thumb.H <= 0)
  {
    DraggingThumb = false;
    return false;
  }
  if (mouseY < metrics.Thumb.Y || mouseY >= metrics.Thumb.Y + metrics.Thumb.H)
  {
    DraggingThumb = false;
    return false;
  }
  DraggingThumb = true;
  GrabOffsetY = mouseY - metrics.Thumb.Y;
  return true;
}

int UGuiScrollbarController::ScrollFromThumbDrag(
    const GuiScrollbarMetrics &metrics, int mouseY) const
{
  if (metrics.MaxScroll <= 0 || metrics.Track.H <= 0)
  {
    return metrics.ScrollY;
  }
  const int thumbTravel = std::max(1, metrics.Track.H - metrics.Thumb.H);
  const int thumbY = mouseY - GrabOffsetY;
  const int relY = std::clamp(thumbY - metrics.Track.Y, 0, thumbTravel);
  const int scroll =
      (relY * metrics.MaxScroll + thumbTravel / 2) / thumbTravel;
  return ClampScroll(scroll, metrics.MaxScroll);
}

int UGuiScrollbarController::ScrollFromTrackClick(
    const GuiScrollbarMetrics &metrics, GuiScrollbarHit hit, int mouseY) const
{
  if (metrics.MaxScroll <= 0)
  {
    return metrics.ScrollY;
  }

  const int page = std::max(1, metrics.ViewportH);
  int scroll = metrics.ScrollY;
  switch (hit)
  {
  case GuiScrollbarHit::TrackAbove:
    scroll -= page;
    break;
  case GuiScrollbarHit::TrackBelow:
    scroll += page;
    break;
  case GuiScrollbarHit::Thumb:
    return ScrollFromThumbDrag(metrics, mouseY);
  case GuiScrollbarHit::None:
  default:
    return metrics.ScrollY;
  }
  return ClampScroll(scroll, metrics.MaxScroll);
}

void UGuiScrollbarController::EndDrag() { DraggingThumb = false; }

} // namespace cutum
