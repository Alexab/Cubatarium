#include "Gui/Widgets/GuiCheckList.h"
#include "Gui/Core/GuiFocus.h"
#include "Gui/Core/GuiKeyCodes.h"
#include "Gui/Core/GuiKeyCodes.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Core/GuiTheme.h"
#include "Gui/Core/GuiTypes.h"

#include <algorithm>

namespace cutum
{

namespace
{

bool ContainsId(const std::vector<std::string> &ids, const std::string &id)
{
  return std::find(ids.begin(), ids.end(), id) != ids.end();
}

} // namespace

UGuiCheckList::UGuiCheckList(const GuiTheme *theme) : Theme(theme)
{
  if (Theme)
  {
    RowHeight = Theme->FontSizeBody + 4;
  }
}

void UGuiCheckList::SetItems(std::vector<GuiCheckListItem> items)
{
  Items = std::move(items);
  if (FocusedIndex >= static_cast<int>(Items.size()))
  {
    FocusedIndex = Items.empty() ? -1 : 0;
  }
  ClampScroll();
  EnsureFocusedVisible();
}

void UGuiCheckList::SetCheckedIds(const std::vector<std::string> &ids)
{
  for (auto &item : Items)
  {
    item.Checked = ContainsId(ids, item.Id);
  }
}

std::vector<std::string> UGuiCheckList::GetCheckedIds() const
{
  std::vector<std::string> result;
  for (const auto &item : Items)
  {
    if (item.Checked)
    {
      result.push_back(item.Id);
    }
  }
  return result;
}

void UGuiCheckList::SetOnChanged(std::function<void()> handler)
{
  OnChanged = std::move(handler);
}

bool UGuiCheckList::MoveFocusedItem(int delta)
{
  if (Items.empty() || delta == 0)
  {
    return false;
  }
  if (FocusedIndex < 0)
  {
    FocusedIndex = 0;
  }
  const int target = FocusedIndex + delta;
  if (target < 0 || target >= static_cast<int>(Items.size()))
  {
    return false;
  }
  std::swap(Items[static_cast<size_t>(FocusedIndex)],
            Items[static_cast<size_t>(target)]);
  FocusedIndex = target;
  EnsureFocusedVisible();
  NotifyChanged();
  return true;
}

bool UGuiCheckList::CanFocus() const
{
  return Enabled && Visible && !Items.empty();
}

void UGuiCheckList::RevealFocused()
{
  if (FocusedIndex < 0 && !Items.empty())
  {
    FocusedIndex = 0;
    EnsureFocusedVisible();
  }
}

bool UGuiCheckList::Activate()
{
  if (!CanFocus())
  {
    return false;
  }
  if (FocusedIndex < 0)
  {
    FocusedIndex = 0;
  }
  return ToggleIndex(FocusedIndex);
}

int UGuiCheckList::ContentHeight() const
{
  return static_cast<int>(Items.size()) * RowHeight;
}

int UGuiCheckList::MaxScrollY() const
{
  return std::max(0, ContentHeight() - Bounds.H);
}

void UGuiCheckList::ClampScroll()
{
  ScrollOffsetPx = std::clamp(ScrollOffsetPx, 0, MaxScrollY());
}

void UGuiCheckList::EnsureFocusedVisible()
{
  if (FocusedIndex < 0 || Items.empty())
  {
    return;
  }
  const int rowTop = FocusedIndex * RowHeight;
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

GuiRect UGuiCheckList::ScrollbarTrackRect() const
{
  if (MaxScrollY() <= 0)
  {
    return {0, 0, 0, 0};
  }
  return {Bounds.X + Bounds.W - kScrollbarWidth, Bounds.Y, kScrollbarWidth,
          Bounds.H};
}

GuiRect UGuiCheckList::ScrollbarThumbRect() const
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

GuiRect UGuiCheckList::ListAreaRect() const
{
  const int bar = MaxScrollY() > 0 ? kScrollbarWidth : 0;
  return {Bounds.X, Bounds.Y, std::max(0, Bounds.W - bar), Bounds.H};
}

void UGuiCheckList::DrawScrollbar(UGuiRenderer &renderer)
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

void UGuiCheckList::Draw(UGuiRenderer &renderer)
{
  if (!Visible || !Theme)
  {
    return;
  }
  renderer.DrawFilledRect(Bounds, Theme->ButtonNormal);
  const GuiRect listArea = ListAreaRect();
  const int box = Theme->FontSizeBody;
  renderer.PushClipRect(listArea);
  int y = Bounds.Y - ScrollOffsetPx;
  for (size_t i = 0; i < Items.size(); ++i)
  {
    GuiRect row{Bounds.X, y, listArea.W, RowHeight};
    if (static_cast<int>(i) == FocusedIndex && HasFocusHighlight())
    {
      renderer.DrawFilledRect(row, Theme->ButtonHover);
    }
    const int boxY = row.Y + (row.H - box) / 2;
    GuiRect boxRect{row.X + Theme->Padding, boxY, box, box};
    renderer.DrawFilledRect(
        boxRect,
        Items[i].Checked ? Theme->ButtonHover : Theme->ButtonNormal);
    renderer.DrawBorderRect(boxRect, Theme->PanelBorder, Theme->BorderThickness);
    if (Items[i].Checked)
    {
      renderer.DrawTextCenteredInRect(boxRect, "x", Theme->TextPrimary);
    }
    const int textX = boxRect.X + box + Theme->Padding;
    renderer.DrawText(Items[i].Label, textX, row.Y + 2, Theme->TextPrimary);
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

bool UGuiCheckList::OnMouseDown(const GuiMouseEvent &event)
{
  if (!Visible || !Enabled || !ListAreaRect().Contains(event.X, event.Y))
  {
    return false;
  }
  if (event.Button != GuiMouseButton::Left)
  {
    return false;
  }
  DragActive = true;
  DragMoved = false;
  ReorderDrag = false;
  DragStartY = event.Y;
  DragStartScroll = ScrollOffsetPx;
  const int localY = event.Y - Bounds.Y + ScrollOffsetPx;
  PendingToggleIndex = localY / RowHeight;
  if (PendingToggleIndex < 0 ||
      PendingToggleIndex >= static_cast<int>(Items.size()))
  {
    PendingToggleIndex = -1;
  }
  return true;
}

bool UGuiCheckList::OnMouseMove(const GuiMouseEvent &event)
{
  if (!DragActive)
  {
    return false;
  }
  const int dy = event.Y - DragStartY;
  if (!DragMoved && std::abs(dy) > kGuiTouchDragSlopPx)
  {
    DragMoved = true;
    if (PendingToggleIndex >= 0)
    {
      ReorderDrag = true;
    }
  }
  if (DragMoved && ReorderDrag && PendingToggleIndex >= 0)
  {
    const int localY = event.Y - Bounds.Y + ScrollOffsetPx;
    int targetIndex = localY / RowHeight;
    targetIndex = std::clamp(targetIndex, 0, static_cast<int>(Items.size()) - 1);
    if (targetIndex != PendingToggleIndex)
    {
      std::swap(Items[static_cast<size_t>(PendingToggleIndex)],
                Items[static_cast<size_t>(targetIndex)]);
      FocusedIndex = targetIndex;
      PendingToggleIndex = targetIndex;
      EnsureFocusedVisible();
      NotifyChanged();
    }
    return true;
  }
  if (DragMoved)
  {
    ScrollOffsetPx = DragStartScroll - dy;
    ClampScroll();
    return true;
  }
  return ListAreaRect().Contains(event.X, event.Y);
}

bool UGuiCheckList::OnMouseUp(const GuiMouseEvent &event)
{
  if (!DragActive)
  {
    return false;
  }
  DragActive = false;
  ReorderDrag = false;
  if (!DragMoved && PendingToggleIndex >= 0)
  {
    FocusIndex(PendingToggleIndex);
    ToggleIndex(PendingToggleIndex);
  }
  PendingToggleIndex = -1;
  return ListAreaRect().Contains(event.X, event.Y) || DragMoved;
}

void UGuiCheckList::NotifyChanged()
{
  if (OnChanged)
  {
    OnChanged();
  }
}

bool UGuiCheckList::FocusIndex(int index)
{
  if (index < 0 || index >= static_cast<int>(Items.size()))
  {
    return false;
  }
  FocusedIndex = index;
  EnsureFocusedVisible();
  return true;
}

bool UGuiCheckList::ToggleIndex(int index)
{
  if (index < 0 || index >= static_cast<int>(Items.size()))
  {
    return false;
  }
  Items[static_cast<size_t>(index)].Checked = !Items[static_cast<size_t>(index)].Checked;
  NotifyChanged();
  return true;
}

bool UGuiCheckList::HandleKeyNavigation(const GuiKeyEvent &event)
{
  if (!Visible || Items.empty())
  {
    return false;
  }
  if (event.Action != GuiKeyAction::Press &&
      event.Action != GuiKeyAction::Repeat)
  {
    return false;
  }

  const int count = static_cast<int>(Items.size());
  int index = FocusedIndex < 0 ? 0 : FocusedIndex;
  const int pageRows = std::max(1, Bounds.H / RowHeight);

  switch (event.KeyCode)
  {
  case GuiKey::Up:
    if ((event.Mods & GuiKey::ModControl) != 0)
    {
      return MoveFocusedItem(-1);
    }
    index = std::max(0, index - 1);
    break;
  case GuiKey::Down:
    if ((event.Mods & GuiKey::ModControl) != 0)
    {
      return MoveFocusedItem(1);
    }
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

  return FocusIndex(index);
}

bool UGuiCheckList::OnKey(const GuiKeyEvent &event)
{
  if (HandleKeyNavigation(event))
  {
    return true;
  }
  return UGuiWidget::OnKey(event);
}

bool UGuiCheckList::OnScroll(const GuiScrollEvent &event)
{
  if (!Visible || Bounds.H <= 0 || MaxScrollY() <= 0)
  {
    return false;
  }
  const int before = ScrollOffsetPx;
  ScrollOffsetPx -= static_cast<int>(event.Yoffset * RowHeight);
  ClampScroll();
  return ScrollOffsetPx != before;
}

bool UGuiCheckList::ScrollAtPoint(int x, int y, const GuiScrollEvent &event)
{
  if (!Visible || !Bounds.Contains(x, y))
  {
    return false;
  }
  return OnScroll(event);
}

} // namespace cutum
