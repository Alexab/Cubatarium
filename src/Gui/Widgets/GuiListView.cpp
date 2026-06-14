#include "GuiListView.h"
#include "Gui/Core/GuiFocus.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Core/GuiTheme.h"

#include "Gui/Core/GuiKeyCodes.h"
#include <algorithm>

namespace cutum
{

UGuiListView::UGuiListView(const GuiTheme *theme) : theme_(theme)
{
  if (theme_)
  {
    rowHeight_ = theme_->fontSizeBody + 4;
  }
}

void UGuiListView::SetItems(std::vector<std::string> items)
{
  items_ = std::move(items);
  if (selectedIndex_ >= static_cast<int>(items_.size()))
  {
    selectedIndex_ = items_.empty() ? -1 : 0;
  }
  ClampScroll();
  EnsureSelectedVisible();
}

void UGuiListView::SetSelectedIndex(int index)
{
  selectedIndex_ = index;
  EnsureSelectedVisible();
}

void UGuiListView::SetOnSelectionChanged(std::function<void(int)> handler)
{
  onSelectionChanged_ = std::move(handler);
}

bool UGuiListView::CanFocus() const
{
  return enabled_ && visible_ && !items_.empty();
}

void UGuiListView::RevealFocused()
{
  if (selectedIndex_ < 0 && !items_.empty())
  {
    selectedIndex_ = 0;
    EnsureSelectedVisible();
  }
}

int UGuiListView::ContentHeight() const
{
  return static_cast<int>(items_.size()) * rowHeight_;
}

int UGuiListView::MaxScrollY() const
{
  return std::max(0, ContentHeight() - bounds_.h);
}

void UGuiListView::ClampScroll()
{
  scrollOffsetPx_ = std::clamp(scrollOffsetPx_, 0, MaxScrollY());
}

void UGuiListView::EnsureSelectedVisible()
{
  if (selectedIndex_ < 0 || items_.empty())
  {
    return;
  }
  const int rowTop = selectedIndex_ * rowHeight_;
  const int rowBottom = rowTop + rowHeight_;
  if (rowTop < scrollOffsetPx_)
  {
    scrollOffsetPx_ = rowTop;
  }
  else if (rowBottom > scrollOffsetPx_ + bounds_.h)
  {
    scrollOffsetPx_ = rowBottom - bounds_.h;
  }
  ClampScroll();
}

GuiRect UGuiListView::ScrollbarTrackRect() const
{
  if (MaxScrollY() <= 0)
  {
    return {0, 0, 0, 0};
  }
  return {bounds_.x + bounds_.w - kScrollbarWidth, bounds_.y, kScrollbarWidth,
          bounds_.h};
}

GuiRect UGuiListView::ScrollbarThumbRect() const
{
  const GuiRect track = ScrollbarTrackRect();
  if (track.h <= 0)
  {
    return track;
  }
  const int contentH = ContentHeight();
  const float ratio =
      static_cast<float>(bounds_.h) / static_cast<float>(contentH);
  const int thumbH = std::max(16, static_cast<int>(track.h * ratio));
  const int maxScroll = MaxScrollY();
  if (maxScroll > 0)
  {
    const int thumbY =
        track.y + (scrollOffsetPx_ * (track.h - thumbH)) / maxScroll;
    return {track.x + 1, thumbY, track.w - 2, thumbH};
  }
  return {track.x + 1, track.y, track.w - 2, thumbH};
}

GuiRect UGuiListView::ListAreaRect() const
{
  const int bar = MaxScrollY() > 0 ? kScrollbarWidth : 0;
  return {bounds_.x, bounds_.y, std::max(0, bounds_.w - bar), bounds_.h};
}

void UGuiListView::DrawScrollbar(UGuiRenderer &renderer)
{
  if (!theme_ || MaxScrollY() <= 0)
  {
    return;
  }
  const GuiRect track = ScrollbarTrackRect();
  renderer.DrawFilledRect(track, theme_->buttonNormal);
  renderer.DrawFilledRect(ScrollbarThumbRect(), theme_->buttonHover);
  renderer.DrawBorderRect(track, theme_->panelBorder, theme_->borderThickness);
}

void UGuiListView::Draw(UGuiRenderer &renderer)
{
  if (!visible_ || !theme_)
  {
    return;
  }
  renderer.DrawFilledRect(bounds_, theme_->buttonNormal);
  const GuiRect listArea = ListAreaRect();
  renderer.PushClipRect(listArea);
  int y = bounds_.y - scrollOffsetPx_;
  for (size_t i = 0; i < items_.size(); ++i)
  {
    GuiRect row{bounds_.x, y, listArea.w, rowHeight_};
    if (static_cast<int>(i) == selectedIndex_)
    {
      renderer.DrawFilledRect(row, theme_->buttonHover);
    }
    renderer.DrawText(items_[i], row.x + theme_->padding, row.y + 2,
                      theme_->textPrimary);
    y += rowHeight_;
  }
  renderer.PopClipRect();
  DrawScrollbar(renderer);
  renderer.DrawBorderRect(bounds_, theme_->panelBorder,
                          theme_->borderThickness);
  if (HasFocusHighlight())
  {
    DrawWidgetFocusRing(renderer, *theme_, bounds_);
  }
}

bool UGuiListView::OnMouseDown(const GuiMouseEvent &event)
{
  if (!visible_ || !ListAreaRect().Contains(event.x, event.y))
  {
    return false;
  }
  const int localY = event.y - bounds_.y + scrollOffsetPx_;
  const int index = localY / rowHeight_;
  if (index >= 0 && index < static_cast<int>(items_.size()))
  {
    selectedIndex_ = index;
    if (onSelectionChanged_)
    {
      onSelectionChanged_(index);
    }
    EnsureSelectedVisible();
    return true;
  }
  return false;
}

bool UGuiListView::SelectIndex(int index)
{
  if (index < 0 || index >= static_cast<int>(items_.size()))
  {
    return false;
  }
  if (selectedIndex_ != index)
  {
    selectedIndex_ = index;
    if (onSelectionChanged_)
    {
      onSelectionChanged_(index);
    }
  }
  EnsureSelectedVisible();
  return true;
}

bool UGuiListView::HandleKeyNavigation(const GuiKeyEvent &event)
{
  if (!acceptKeyNavigation_ || !visible_ || items_.empty())
  {
    return false;
  }
  if (event.action != GuiKeyAction::Press &&
      event.action != GuiKeyAction::Repeat)
  {
    return false;
  }

  const int count = static_cast<int>(items_.size());
  int index = selectedIndex_ < 0 ? 0 : selectedIndex_;
  const int pageRows = std::max(1, bounds_.h / rowHeight_);

  switch (event.keyCode)
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
  if (!visible_ || bounds_.h <= 0)
  {
    return false;
  }
  scrollOffsetPx_ -= static_cast<int>(event.yoffset * rowHeight_);
  ClampScroll();
  return true;
}

bool UGuiListView::ScrollAtPoint(int x, int y, const GuiScrollEvent &event)
{
  if (!visible_ || !bounds_.Contains(x, y))
  {
    return false;
  }
  return OnScroll(event);
}

} // namespace cutum
