#include "Gui/Widgets/GuiScrollView.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Core/GuiTheme.h"
#include "Gui/Core/GuiTypes.h"
#include "Gui/Layout/GuiLayout.h"

#include "Gui/Core/GuiKeyCodes.h"
#include <algorithm>

namespace cutum
{

namespace
{

bool IsDescendantOf(const UGuiWidget *root, const UGuiWidget *target)
{
  if (!root || !target)
  {
    return false;
  }
  if (root == target)
  {
    return true;
  }
  for (const auto &child : root->GetChildren())
  {
    if (IsDescendantOf(child.get(), target))
    {
      return true;
    }
  }
  return false;
}

bool ShouldShowScrollbar(GuiScrollbarMode mode, int maxScrollY)
{
  switch (mode)
  {
  case GuiScrollbarMode::Always:
    return true;
  case GuiScrollbarMode::Hidden:
    return false;
  case GuiScrollbarMode::Auto:
  default:
    return maxScrollY > 0;
  }
}

bool IsScrollInteractionEnabled(GuiScrollbarMode mode)
{
  return mode != GuiScrollbarMode::Hidden;
}

int MeasureVisibleContentExtent(const UGuiPanel &content, int contentTop)
{
  int maxBottom = 0;
  for (const auto &child : content.GetChildren())
  {
    if (!child || !child->IsVisible())
    {
      continue;
    }
    const GuiRect &b = child->GetBounds();
    maxBottom = std::max(maxBottom, b.Y + b.H - contentTop);
  }
  return maxBottom;
}

} // namespace

UGuiScrollView::UGuiScrollView(const GuiTheme *theme)
    : Theme(theme), ContentPanel(theme)
{
  ContentPanel.SetDrawBackground(false);
}

UGuiPanel &UGuiScrollView::Content() { return ContentPanel; }

const UGuiPanel &UGuiScrollView::Content() const { return ContentPanel; }

GuiRect UGuiScrollView::ViewportRect() const
{
  const int bar =
      ShouldShowScrollbar(ScrollbarMode, MaxScrollY()) ? kScrollbarWidth : 0;
  return {Bounds.X, Bounds.Y, std::max(0, Bounds.W - bar), Bounds.H};
}

GuiRect UGuiScrollView::ScrollbarTrackRect() const
{
  if (!ShouldShowScrollbar(ScrollbarMode, MaxScrollY()))
  {
    return {0, 0, 0, 0};
  }
  return {Bounds.X + Bounds.W - kScrollbarWidth, Bounds.Y, kScrollbarWidth,
          Bounds.H};
}

GuiRect UGuiScrollView::ScrollbarThumbRect() const
{
  const GuiRect track = ScrollbarTrackRect();
  if (track.H <= 0 || ScrollContentHeight <= 0)
  {
    return track;
  }
  const int maxScroll = MaxScrollY();
  const float ratio =
      static_cast<float>(Bounds.H) / static_cast<float>(ScrollContentHeight);
  const int thumbH = std::max(16, static_cast<int>(track.H * ratio));
  if (maxScroll > 0)
  {
    const int thumbY = track.Y + (ScrollY * (track.H - thumbH)) / maxScroll;
    return {track.X + 1, thumbY, track.W - 2, thumbH};
  }
  return {track.X + 1, track.Y, track.W - 2, thumbH};
}

int UGuiScrollView::MaxScrollY() const
{
  return std::max(0, ScrollContentHeight - Bounds.H);
}

void UGuiScrollView::ClampScroll()
{
  ScrollY = std::clamp(ScrollY, 0, MaxScrollY());
}

void UGuiScrollView::SetScrollY(int y)
{
  ScrollY = y;
  ClampScroll();
}

void UGuiScrollView::SetAfterScrollLayout(AfterScrollLayoutFn callback)
{
  AfterScrollLayout = std::move(callback);
}

bool UGuiScrollView::ContainsWidget(const UGuiWidget *widget) const
{
  return IsDescendantOf(&ContentPanel, widget);
}

void UGuiScrollView::EnsureWidgetVisible(const UGuiWidget &widget)
{
  if (!IsScrollInteractionEnabled(ScrollbarMode))
  {
    return;
  }
  const GuiRect &b = widget.GetBounds();
  const GuiRect vp = ViewportRect();
  if (b.H <= 0 || vp.H <= 0)
  {
    return;
  }
  if (b.Y < vp.Y)
  {
    ScrollY -= vp.Y - b.Y;
  }
  else if (b.Y + b.H > vp.Y + vp.H)
  {
    ScrollY += (b.Y + b.H) - (vp.Y + vp.H);
  }
  ClampScroll();
  LayoutContent(LayoutSpacing, LayoutPadding);
}

void UGuiScrollView::LayoutContent(int spacing, int Padding)
{
  LayoutSpacing = spacing;
  LayoutPadding = Padding;
  const GuiRect vp = ViewportRect();
  const int vpW = std::max(1, vp.W);

  if (AfterScrollLayout)
  {
    if (ScrollContentHeight <= 0)
    {
      ScrollContentHeight = std::max(1, vp.H);
    }
    ContentPanel.SetBounds({vp.X, vp.Y - ScrollY, vpW, ScrollContentHeight});
    AfterScrollLayout(*this);
    const int contentTop = vp.Y - ScrollY;
    ScrollContentHeight =
        std::max(vp.H, MeasureVisibleContentExtent(ContentPanel, contentTop));
    ClampScroll();
    ContentPanel.SetBounds({vp.X, vp.Y - ScrollY, vpW, ScrollContentHeight});
    AfterScrollLayout(*this);
    return;
  }

  std::vector<UGuiWidget *> kids;
  for (const auto &child : ContentPanel.GetChildren())
  {
    kids.push_back(child.get());
  }
  ScrollContentHeight = UGuiLayout::StackVerticalMeasure({0, 0, vpW, 100000}, spacing,
                                                   Padding, kids);

  ContentPanel.SetBounds({vp.X, vp.Y - ScrollY, vpW, ScrollContentHeight});
  UGuiLayout::StackVertical({vp.X, vp.Y - ScrollY, vpW, ScrollContentHeight}, spacing,
                            Padding, kids);
  ClampScroll();
}

void UGuiScrollView::DrawScrollbar(UGuiRenderer &renderer)
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

void UGuiScrollView::Draw(UGuiRenderer &renderer)
{
  if (!Visible || !Theme)
  {
    return;
  }
  const GuiRect vp = ViewportRect();
  renderer.DrawFilledRect(Bounds, Theme->ButtonNormal);
  renderer.PushClipRect(vp);
  ContentPanel.Draw(renderer);
  renderer.PopClipRect();
  DrawScrollbar(renderer);
  renderer.DrawBorderRect(Bounds, Theme->PanelBorder, Theme->BorderThickness);
}

UGuiWidget *UGuiScrollView::HitTestFocusable(int x, int y)
{
  if (!Visible || !Bounds.Contains(x, y))
  {
    return nullptr;
  }
  const GuiRect vp = ViewportRect();
  if (!vp.Contains(x, y))
  {
    return nullptr;
  }
  return ContentPanel.HitTestFocusable(x, y);
}

UGuiWidget *UGuiScrollView::HitTest(int x, int y)
{
  if (!Visible || !Bounds.Contains(x, y))
  {
    return nullptr;
  }
  const GuiRect vp = ViewportRect();
  if (UGuiWidget *hit = ContentPanel.HitTest(x, y))
  {
    if (vp.Contains(x, y))
    {
      return hit;
    }
    return nullptr;
  }
  if (ViewportRect().Contains(x, y) || ScrollbarTrackRect().Contains(x, y))
  {
    return this;
  }
  return nullptr;
}

bool UGuiScrollView::BeginDeferredTouch(const GuiMouseEvent &event)
{
  if (!ViewportRect().Contains(event.X, event.Y) &&
      !ScrollbarTrackRect().Contains(event.X, event.Y))
  {
    return false;
  }
  if (MaxScrollY() <= 0)
  {
    return false;
  }
  DeferredTouchActive = true;
  DeferredDragged = false;
  DeferredDown = event;
  DeferredDragStartY = event.Y;
  DeferredDragStartScroll = ScrollY;
  return true;
}

bool UGuiScrollView::OnDeferredMove(const GuiMouseEvent &event)
{
  if (!DeferredTouchActive)
  {
    return false;
  }
  const int dy = event.Y - DeferredDragStartY;
  if (!DeferredDragged && std::abs(dy) > kGuiTouchDragSlopPx)
  {
    DeferredDragged = true;
  }
  if (DeferredDragged)
  {
    ScrollY = DeferredDragStartScroll - dy;
    ClampScroll();
    LayoutContent(LayoutSpacing, LayoutPadding);
    return true;
  }
  return false;
}

bool UGuiScrollView::OnDeferredUp(const GuiMouseEvent &event)
{
  if (!DeferredTouchActive)
  {
    return false;
  }
  DeferredTouchActive = false;
  if (!DeferredDragged)
  {
    ContentPanel.OnMouseDown(DeferredDown);
    ContentPanel.OnMouseUp(event);
    return true;
  }
  DeferredDragged = false;
  return true;
}

bool UGuiScrollView::OnMouseDown(const GuiMouseEvent &event)
{
  if (ContentPanel.HitTest(event.X, event.Y) &&
      ContentPanel.OnMouseDown(event))
  {
    return true;
  }
  if (BeginDeferredTouch(event))
  {
    return true;
  }
  return ContentPanel.OnMouseDown(event);
}

bool UGuiScrollView::OnMouseUp(const GuiMouseEvent &event)
{
  if (DeferredTouchActive)
  {
    return OnDeferredUp(event);
  }
  return ContentPanel.OnMouseUp(event);
}

bool UGuiScrollView::OnMouseMove(const GuiMouseEvent &event)
{
  if (DeferredTouchActive)
  {
    return OnDeferredMove(event);
  }
  return ContentPanel.OnMouseMove(event);
}

bool UGuiScrollView::OnKey(const GuiKeyEvent &event)
{
  if (ContentPanel.OnKey(event))
  {
    return true;
  }
  return HandleKeyScroll(event);
}

bool UGuiScrollView::OnChar(const GuiCharEvent &event)
{
  return ContentPanel.OnChar(event);
}

bool UGuiScrollView::HandleKeyScroll(const GuiKeyEvent &event)
{
  if (!Visible || !IsScrollInteractionEnabled(ScrollbarMode))
  {
    return false;
  }
  if (event.Action != GuiKeyAction::Press &&
      event.Action != GuiKeyAction::Repeat)
  {
    return false;
  }
  const int step = 24;
  switch (event.KeyCode)
  {
  case GuiKey::Up:
    ScrollY -= step;
    break;
  case GuiKey::Down:
    ScrollY += step;
    break;
  case GuiKey::PageUp:
    ScrollY -= Bounds.H;
    break;
  case GuiKey::PageDown:
    ScrollY += Bounds.H;
    break;
  case GuiKey::Home:
    ScrollY = 0;
    break;
  case GuiKey::End:
    ScrollY = MaxScrollY();
    break;
  default:
    return false;
  }
  ClampScroll();
  LayoutContent();
  return true;
}

bool UGuiScrollView::OnScroll(const GuiScrollEvent &event)
{
  if (!Visible || Bounds.H <= 0 || !IsScrollInteractionEnabled(ScrollbarMode))
  {
    return false;
  }
  ScrollY -= static_cast<int>(event.Yoffset * 24);
  ClampScroll();
  LayoutContent();
  return true;
}

bool UGuiScrollView::ScrollAtPoint(int x, int y, const GuiScrollEvent &event)
{
  if (!Visible || !Bounds.Contains(x, y))
  {
    return false;
  }
  if (ContentPanel.ScrollAtPoint(x, y, event))
  {
    return true;
  }
  if (!IsScrollInteractionEnabled(ScrollbarMode))
  {
    return false;
  }
  return OnScroll(event);
}

void UGuiScrollView::CollectFocusables(std::vector<UGuiWidget *> &out)
{
  if (!Visible || !Enabled)
  {
    return;
  }
  ContentPanel.CollectFocusables(out);
}

} // namespace cutum
