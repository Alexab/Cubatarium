#include "Gui/Widgets/GuiListView.h"
#include "Gui/Core/GuiFocus.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Core/GuiTheme.h"
#include "Gui/Core/GuiTypes.h"

#include "Gui/Core/GuiKeyCodes.h"
#include <algorithm>

namespace cutum
{

UGuiListView::UGuiListView(const GuiTheme *theme) : Theme(theme)
{
  if (Theme)
  {
    RowHeight = Theme->FontSizeBody + 4;
  }
}

void UGuiListView::SetItems(std::vector<std::string> items)
{
  Items = std::move(items);
  if (SelectedIndex >= static_cast<int>(Items.size()))
  {
    SelectedIndex = Items.empty() ? -1 : 0;
  }
  ClampScroll();
  EnsureSelectedVisible();
}

void UGuiListView::SetSelectedIndex(int index)
{
  SelectedIndex = index;
  EnsureSelectedVisible();
}

void UGuiListView::SetOnSelectionChanged(std::function<void(int)> handler)
{
  OnSelectionChanged = std::move(handler);
}

bool UGuiListView::CanFocus() const
{
  return Enabled && Visible && !Items.empty();
}

void UGuiListView::RevealFocused()
{
  if (SelectedIndex < 0 && !Items.empty())
  {
    SelectedIndex = 0;
    EnsureSelectedVisible();
  }
}

int UGuiListView::ContentHeight() const
{
  return static_cast<int>(Items.size()) * RowHeight;
}

int UGuiListView::MaxScrollY() const
{
  return std::max(0, ContentHeight() - Bounds.H);
}

void UGuiListView::ClampScroll()
{
  ScrollOffsetPx = std::clamp(ScrollOffsetPx, 0, MaxScrollY());
}

void UGuiListView::ScrollToEnd() { ScrollOffsetPx = MaxScrollY(); }

void UGuiListView::EnsureSelectedVisible()
{
  if (SelectedIndex < 0 || Items.empty())
  {
    return;
  }
  const int rowTop = SelectedIndex * RowHeight;
  const int rowBottom = rowTop + RowHeight;
  if (rowTop < ScrollOffsetPx)
  {
    ScrollOffsetPx = rowTop;
  }
  else if (rowBottom > ScrollOffsetPx + Bounds.H)
  {
    ScrollOffsetPx = rowBottom - Bounds.H;
  }
  ClampScroll();
}

GuiRect UGuiListView::ScrollbarTrackRect() const
{
  if (MaxScrollY() <= 0)
  {
    return {0, 0, 0, 0};
  }
  return {Bounds.X + Bounds.W - kScrollbarWidth, Bounds.Y, kScrollbarWidth,
          Bounds.H};
}

GuiRect UGuiListView::ScrollbarThumbRect() const
{
  const GuiRect track = ScrollbarTrackRect();
  if (track.H <= 0)
  {
    return track;
  }
  const int contentH = ContentHeight();
  const float ratio =
      static_cast<float>(Bounds.H) / static_cast<float>(contentH);
  const int thumbH = std::max(16, static_cast<int>(track.H * ratio));
  const int maxScroll = MaxScrollY();
  if (maxScroll > 0)
  {
    const int thumbY =
        track.Y + (ScrollOffsetPx * (track.H - thumbH)) / maxScroll;
    return {track.X + 1, thumbY, track.W - 2, thumbH};
  }
  return {track.X + 1, track.Y, track.W - 2, thumbH};
}

GuiRect UGuiListView::ListAreaRect() const
{
  const int bar = MaxScrollY() > 0 ? kScrollbarWidth : 0;
  return {Bounds.X, Bounds.Y, std::max(0, Bounds.W - bar), Bounds.H};
}

void UGuiListView::DrawScrollbar(UGuiRenderer &renderer)
{
  if (!Theme || MaxScrollY() <= 0)
  {
    return;
  }
  const GuiRect track = ScrollbarTrackRect();
  renderer.DrawFilledRect(track, Theme->ButtonNormal);
  renderer.DrawFilledRect(ScrollbarThumbRect(), Theme->ButtonHover);
  renderer.DrawBorderRect(track, Theme->PanelBorder, Theme->BorderThickness);
}

void UGuiListView::Draw(UGuiRenderer &renderer)
{
  if (!Visible || !Theme)
  {
    return;
  }
  renderer.DrawFilledRect(Bounds, Theme->ButtonNormal);
  const GuiRect listArea = ListAreaRect();
  renderer.PushClipRect(listArea);
  int y = Bounds.Y - ScrollOffsetPx;
  for (size_t i = 0; i < Items.size(); ++i)
  {
    GuiRect row{Bounds.X, y, listArea.W, RowHeight};
    if (static_cast<int>(i) == SelectedIndex)
    {
      renderer.DrawFilledRect(row, Theme->ButtonHover);
    }
    renderer.DrawText(Items[i], row.X + Theme->Padding, row.Y + 2,
                      Theme->TextPrimary);
    y += RowHeight;
  }
  renderer.PopClipRect();
  DrawScrollbar(renderer);
  renderer.DrawBorderRect(Bounds, Theme->PanelBorder, Theme->BorderThickness);
  if (HasFocusHighlight())
  {
    DrawWidgetFocusRing(renderer, *Theme, Bounds);
  }
}

bool UGuiListView::OnMouseDown(const GuiMouseEvent &event)
{
  if (!Visible || !ListAreaRect().Contains(event.X, event.Y))
  {
    return false;
  }
  DragActive = true;
  DragMoved = false;
  DragStartY = event.Y;
  DragStartScroll = ScrollOffsetPx;
  const int localY = event.Y - Bounds.Y + ScrollOffsetPx;
  PendingSelectIndex = localY / RowHeight;
  if (PendingSelectIndex < 0 ||
      PendingSelectIndex >= static_cast<int>(Items.size()))
  {
    PendingSelectIndex = -1;
  }
  return true;
}

bool UGuiListView::OnMouseMove(const GuiMouseEvent &event)
{
  if (!DragActive)
  {
    return false;
  }
  const int dy = event.Y - DragStartY;
  if (!DragMoved && std::abs(dy) > kGuiTouchDragSlopPx)
  {
    DragMoved = true;
  }
  if (DragMoved)
  {
    ScrollOffsetPx = DragStartScroll - dy;
    ClampScroll();
    return true;
  }
  return ListAreaRect().Contains(event.X, event.Y);
}

bool UGuiListView::OnMouseUp(const GuiMouseEvent &event)
{
  if (!DragActive)
  {
    return false;
  }
  DragActive = false;
  if (!DragMoved && PendingSelectIndex >= 0)
  {
    SelectIndex(PendingSelectIndex);
  }
  PendingSelectIndex = -1;
  return ListAreaRect().Contains(event.X, event.Y) || DragMoved;
}

bool UGuiListView::SelectIndex(int index)
{
  if (index < 0 || index >= static_cast<int>(Items.size()))
  {
    return false;
  }
  if (SelectedIndex != index)
  {
    SelectedIndex = index;
    if (OnSelectionChanged)
    {
      OnSelectionChanged(index);
    }
  }
  EnsureSelectedVisible();
  return true;
}

bool UGuiListView::HandleKeyNavigation(const GuiKeyEvent &event)
{
  if (!AcceptKeyNavigation || !Visible || Items.empty())
  {
    return false;
  }
  if (event.Action != GuiKeyAction::Press &&
      event.Action != GuiKeyAction::Repeat)
  {
    return false;
  }

  const int count = static_cast<int>(Items.size());
  int index = SelectedIndex < 0 ? 0 : SelectedIndex;
  const int pageRows = std::max(1, Bounds.H / RowHeight);

  switch (event.KeyCode)
  {
  case GuiKey::Up:
    index = std::max(0, index - 1);
    break;
  case GuiKey::Down:
    index = std::min(count - 1, index + 1);
    break;
  case GuiKey::Home:
    index = 0;
    break;
  case GuiKey::End:
    index = count - 1;
    break;
  case GuiKey::PageUp:
    index = std::max(0, index - pageRows);
    break;
  case GuiKey::PageDown:
    index = std::min(count - 1, index + pageRows);
    break;
  default:
    return false;
  }

  return SelectIndex(index);
}

bool UGuiListView::OnKey(const GuiKeyEvent &event)
{
  if (HandleKeyNavigation(event))
  {
    return true;
  }
  return UGuiWidget::OnKey(event);
}

bool UGuiListView::OnScroll(const GuiScrollEvent &event)
{
  if (!Visible || Bounds.H <= 0)
  {
    return false;
  }
  ScrollOffsetPx -= static_cast<int>(event.Yoffset * RowHeight);
  ClampScroll();
  return true;
}

bool UGuiListView::ScrollAtPoint(int x, int y, const GuiScrollEvent &event)
{
  if (!Visible || !Bounds.Contains(x, y))
  {
    return false;
  }
  return OnScroll(event);
}

} // namespace cutum
