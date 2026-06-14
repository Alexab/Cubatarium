#include "GuiScrollView.h"
#include "Gui/GuiRenderer.h"
#include "Gui/GuiTheme.h"
#include "Gui/Layout/GuiLayout.h"

#include <GLFW/glfw3.h>
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

} // namespace

UGuiScrollView::UGuiScrollView(const GuiTheme *theme)
    : theme_(theme), content_(theme)
{
  content_.SetDrawBackground(false);
}

UGuiPanel &UGuiScrollView::Content() { return content_; }

const UGuiPanel &UGuiScrollView::Content() const { return content_; }

GuiRect UGuiScrollView::ViewportRect() const
{
  const int bar =
      ShouldShowScrollbar(scrollbarMode_, MaxScrollY()) ? kScrollbarWidth : 0;
  return {bounds_.x, bounds_.y, std::max(0, bounds_.w - bar), bounds_.h};
}

GuiRect UGuiScrollView::ScrollbarTrackRect() const
{
  if (!ShouldShowScrollbar(scrollbarMode_, MaxScrollY()))
  {
    return {0, 0, 0, 0};
  }
  return {bounds_.x + bounds_.w - kScrollbarWidth, bounds_.y, kScrollbarWidth,
          bounds_.h};
}

GuiRect UGuiScrollView::ScrollbarThumbRect() const
{
  const GuiRect track = ScrollbarTrackRect();
  if (track.h <= 0 || contentHeight_ <= 0)
  {
    return track;
  }
  const int maxScroll = MaxScrollY();
  const float ratio =
      static_cast<float>(bounds_.h) / static_cast<float>(contentHeight_);
  const int thumbH = std::max(16, static_cast<int>(track.h * ratio));
  if (maxScroll > 0)
  {
    const int thumbY = track.y + (scrollY_ * (track.h - thumbH)) / maxScroll;
    return {track.x + 1, thumbY, track.w - 2, thumbH};
  }
  return {track.x + 1, track.y, track.w - 2, thumbH};
}

int UGuiScrollView::MaxScrollY() const
{
  return std::max(0, contentHeight_ - bounds_.h);
}

void UGuiScrollView::ClampScroll()
{
  scrollY_ = std::clamp(scrollY_, 0, MaxScrollY());
}

void UGuiScrollView::SetScrollY(int y)
{
  scrollY_ = y;
  ClampScroll();
}

void UGuiScrollView::SetAfterScrollLayout(AfterScrollLayoutFn callback)
{
  afterScrollLayout_ = std::move(callback);
}

bool UGuiScrollView::ContainsWidget(const UGuiWidget *widget) const
{
  return IsDescendantOf(&content_, widget);
}

void UGuiScrollView::EnsureWidgetVisible(const UGuiWidget &widget)
{
  if (!IsScrollInteractionEnabled(scrollbarMode_))
  {
    return;
  }
  const GuiRect &b = widget.GetBounds();
  const GuiRect vp = ViewportRect();
  if (b.h <= 0 || vp.h <= 0)
  {
    return;
  }
  if (b.y < vp.y)
  {
    scrollY_ -= vp.y - b.y;
  }
  else if (b.y + b.h > vp.y + vp.h)
  {
    scrollY_ += (b.y + b.h) - (vp.y + vp.h);
  }
  ClampScroll();
  LayoutContent(layoutSpacing_, layoutPadding_);
}

void UGuiScrollView::LayoutContent(int spacing, int padding)
{
  layoutSpacing_ = spacing;
  layoutPadding_ = padding;
  const GuiRect vp = ViewportRect();
  const int vpW = std::max(1, vp.w);

  if (afterScrollLayout_)
  {
    if (contentHeight_ <= 0)
    {
      contentHeight_ = std::max(1, vp.h);
    }
    content_.SetBounds({vp.x, vp.y - scrollY_, vpW, contentHeight_});
    afterScrollLayout_(*this);
    contentHeight_ = std::max(vp.h, content_.GetBounds().h);
    ClampScroll();
    content_.SetBounds({vp.x, vp.y - scrollY_, vpW, contentHeight_});
    afterScrollLayout_(*this);
    return;
  }

  std::vector<UGuiWidget *> kids;
  for (const auto &child : content_.GetChildren())
  {
    kids.push_back(child.get());
  }
  contentHeight_ = UGuiLayout::StackVerticalMeasure({0, 0, vpW, 100000},
                                                    spacing, padding, kids);

  content_.SetBounds({vp.x, vp.y - scrollY_, vpW, contentHeight_});
  UGuiLayout::StackVertical({vp.x, vp.y - scrollY_, vpW, contentHeight_},
                            spacing, padding, kids);
  ClampScroll();
  if (afterScrollLayout_)
  {
    afterScrollLayout_(*this);
  }
}

void UGuiScrollView::DrawScrollbar(UGuiRenderer &renderer)
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

void UGuiScrollView::Draw(UGuiRenderer &renderer)
{
  if (!visible_ || !theme_)
  {
    return;
  }
  const GuiRect vp = ViewportRect();
  renderer.DrawFilledRect(bounds_, theme_->buttonNormal);
  renderer.PushClipRect(vp);
  content_.Draw(renderer);
  renderer.PopClipRect();
  DrawScrollbar(renderer);
  renderer.DrawBorderRect(bounds_, theme_->panelBorder,
                          theme_->borderThickness);
}

UGuiWidget *UGuiScrollView::HitTestFocusable(int x, int y)
{
  if (!visible_ || !bounds_.Contains(x, y))
  {
    return nullptr;
  }
  return content_.HitTestFocusable(x, y);
}

UGuiWidget *UGuiScrollView::HitTest(int x, int y)
{
  if (!visible_ || !bounds_.Contains(x, y))
  {
    return nullptr;
  }
  if (UGuiWidget *hit = content_.HitTest(x, y))
  {
    return hit;
  }
  if (ViewportRect().Contains(x, y) || ScrollbarTrackRect().Contains(x, y))
  {
    return this;
  }
  return nullptr;
}

bool UGuiScrollView::OnMouseDown(const GuiMouseEvent &event)
{
  return content_.OnMouseDown(event);
}

bool UGuiScrollView::OnMouseUp(const GuiMouseEvent &event)
{
  return content_.OnMouseUp(event);
}

bool UGuiScrollView::OnMouseMove(const GuiMouseEvent &event)
{
  return content_.OnMouseMove(event);
}

bool UGuiScrollView::OnKey(const GuiKeyEvent &event)
{
  if (content_.OnKey(event))
  {
    return true;
  }
  return HandleKeyScroll(event);
}

bool UGuiScrollView::OnChar(const GuiCharEvent &event)
{
  return content_.OnChar(event);
}

bool UGuiScrollView::HandleKeyScroll(const GuiKeyEvent &event)
{
  if (!visible_ || !IsScrollInteractionEnabled(scrollbarMode_))
  {
    return false;
  }
  if (event.action != GuiKeyAction::Press &&
      event.action != GuiKeyAction::Repeat)
  {
    return false;
  }
  const int step = 24;
  switch (event.keyCode)
  {
  case GLFW_KEY_UP:
    scrollY_ -= step;
    break;
  case GLFW_KEY_DOWN:
    scrollY_ += step;
    break;
  case GLFW_KEY_PAGE_UP:
    scrollY_ -= bounds_.h;
    break;
  case GLFW_KEY_PAGE_DOWN:
    scrollY_ += bounds_.h;
    break;
  case GLFW_KEY_HOME:
    scrollY_ = 0;
    break;
  case GLFW_KEY_END:
    scrollY_ = MaxScrollY();
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
  if (!visible_ || bounds_.h <= 0 ||
      !IsScrollInteractionEnabled(scrollbarMode_))
  {
    return false;
  }
  scrollY_ -= static_cast<int>(event.yoffset * 24);
  ClampScroll();
  LayoutContent();
  return true;
}

bool UGuiScrollView::ScrollAtPoint(int x, int y, const GuiScrollEvent &event)
{
  if (!visible_ || !bounds_.Contains(x, y) ||
      !IsScrollInteractionEnabled(scrollbarMode_))
  {
    return false;
  }
  return OnScroll(event);
}

void UGuiScrollView::CollectFocusables(std::vector<UGuiWidget *> &out)
{
  if (!visible_ || !enabled_)
  {
    return;
  }
  content_.CollectFocusables(out);
}

} // namespace cutum
