#include "GuiScrollView.h"
#include "Gui/GuiRenderer.h"
#include "Gui/GuiTheme.h"
#include "Gui/Layout/GuiLayout.h"

#include <GLFW/glfw3.h>
#include <algorithm>

namespace cutum {

namespace {

bool IsDescendantOf(const GuiWidget* root, const GuiWidget* target)
{
    if (!root || !target) {
        return false;
    }
    if (root == target) {
        return true;
    }
    for (const auto& child : root->GetChildren()) {
        if (IsDescendantOf(child.get(), target)) {
            return true;
        }
    }
    return false;
}

bool ShouldShowScrollbar(GuiScrollbarMode mode, int maxScrollY)
{
    switch (mode) {
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

GuiScrollView::GuiScrollView(const GuiTheme* theme)
    : theme_(theme)
    , content_(theme)
{
    content_.SetDrawBackground(false);
}

GuiPanel& GuiScrollView::Content()
{
    return content_;
}

const GuiPanel& GuiScrollView::Content() const
{
    return content_;
}

GuiRect GuiScrollView::ViewportRect() const
{
    const int bar = ShouldShowScrollbar(scrollbarMode_, MaxScrollY()) ? kScrollbarWidth : 0;
    return {bounds_.x, bounds_.y, std::max(0, bounds_.w - bar), bounds_.h};
}

GuiRect GuiScrollView::ScrollbarTrackRect() const
{
    if (!ShouldShowScrollbar(scrollbarMode_, MaxScrollY())) {
        return {0, 0, 0, 0};
    }
    return {bounds_.x + bounds_.w - kScrollbarWidth, bounds_.y, kScrollbarWidth, bounds_.h};
}

GuiRect GuiScrollView::ScrollbarThumbRect() const
{
    const GuiRect track = ScrollbarTrackRect();
    if (track.h <= 0 || contentHeight_ <= 0) {
        return track;
    }
    const int maxScroll = MaxScrollY();
    const float ratio = static_cast<float>(bounds_.h) / static_cast<float>(contentHeight_);
    const int thumbH = std::max(16, static_cast<int>(track.h * ratio));
    if (maxScroll > 0) {
        const int thumbY = track.y + (scrollY_ * (track.h - thumbH)) / maxScroll;
        return {track.x + 1, thumbY, track.w - 2, thumbH};
    }
    return {track.x + 1, track.y, track.w - 2, thumbH};
}

int GuiScrollView::MaxScrollY() const
{
    return std::max(0, contentHeight_ - bounds_.h);
}

void GuiScrollView::ClampScroll()
{
    scrollY_ = std::clamp(scrollY_, 0, MaxScrollY());
}

void GuiScrollView::SetScrollY(int y)
{
    scrollY_ = y;
    ClampScroll();
}

void GuiScrollView::SetAfterScrollLayout(AfterScrollLayoutFn callback)
{
    afterScrollLayout_ = std::move(callback);
}

bool GuiScrollView::ContainsWidget(const GuiWidget* widget) const
{
    return IsDescendantOf(&content_, widget);
}

void GuiScrollView::EnsureWidgetVisible(const GuiWidget& widget)
{
    if (!IsScrollInteractionEnabled(scrollbarMode_)) {
        return;
    }
    const GuiRect& b = widget.GetBounds();
    const GuiRect vp = ViewportRect();
    if (b.h <= 0 || vp.h <= 0) {
        return;
    }
    if (b.y < vp.y) {
        scrollY_ -= vp.y - b.y;
    } else if (b.y + b.h > vp.y + vp.h) {
        scrollY_ += (b.y + b.h) - (vp.y + vp.h);
    }
    ClampScroll();
    LayoutContent(layoutSpacing_, layoutPadding_);
}

void GuiScrollView::LayoutContent(int spacing, int padding)
{
    layoutSpacing_ = spacing;
    layoutPadding_ = padding;
    const GuiRect vp = ViewportRect();
    const int vpW = std::max(1, vp.w);

    if (afterScrollLayout_) {
        if (contentHeight_ <= 0) {
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

    std::vector<GuiWidget*> kids;
    for (const auto& child : content_.GetChildren()) {
        kids.push_back(child.get());
    }
    contentHeight_ = GuiLayout::StackVerticalMeasure({0, 0, vpW, 100000}, spacing, padding, kids);

    content_.SetBounds({vp.x, vp.y - scrollY_, vpW, contentHeight_});
    GuiLayout::StackVertical({vp.x, vp.y - scrollY_, vpW, contentHeight_}, spacing, padding, kids);
    ClampScroll();
    if (afterScrollLayout_) {
        afterScrollLayout_(*this);
    }
}

void GuiScrollView::DrawScrollbar(GuiRenderer& renderer)
{
    if (!theme_ || MaxScrollY() <= 0) {
        return;
    }
    const GuiRect track = ScrollbarTrackRect();
    renderer.DrawFilledRect(track, theme_->buttonNormal);
    renderer.DrawFilledRect(ScrollbarThumbRect(), theme_->buttonHover);
    renderer.DrawBorderRect(track, theme_->panelBorder, theme_->borderThickness);
}

void GuiScrollView::Draw(GuiRenderer& renderer)
{
    if (!visible_ || !theme_) {
        return;
    }
    const GuiRect vp = ViewportRect();
    renderer.DrawFilledRect(bounds_, theme_->buttonNormal);
    renderer.PushClipRect(vp);
    content_.Draw(renderer);
    renderer.PopClipRect();
    DrawScrollbar(renderer);
    renderer.DrawBorderRect(bounds_, theme_->panelBorder, theme_->borderThickness);
}

GuiWidget* GuiScrollView::HitTestFocusable(int x, int y)
{
    if (!visible_ || !bounds_.Contains(x, y)) {
        return nullptr;
    }
    return content_.HitTestFocusable(x, y);
}

GuiWidget* GuiScrollView::HitTest(int x, int y)
{
    if (!visible_ || !bounds_.Contains(x, y)) {
        return nullptr;
    }
    if (GuiWidget* hit = content_.HitTest(x, y)) {
        return hit;
    }
    if (ViewportRect().Contains(x, y) || ScrollbarTrackRect().Contains(x, y)) {
        return this;
    }
    return nullptr;
}

bool GuiScrollView::OnMouseDown(const GuiMouseEvent& event)
{
    return content_.OnMouseDown(event);
}

bool GuiScrollView::OnMouseUp(const GuiMouseEvent& event)
{
    return content_.OnMouseUp(event);
}

bool GuiScrollView::OnMouseMove(const GuiMouseEvent& event)
{
    return content_.OnMouseMove(event);
}

bool GuiScrollView::OnKey(const GuiKeyEvent& event)
{
    if (content_.OnKey(event)) {
        return true;
    }
    return HandleKeyScroll(event);
}

bool GuiScrollView::OnChar(const GuiCharEvent& event)
{
    return content_.OnChar(event);
}

bool GuiScrollView::HandleKeyScroll(const GuiKeyEvent& event)
{
    if (!visible_ || !IsScrollInteractionEnabled(scrollbarMode_)) {
        return false;
    }
    if (event.action != GuiKeyAction::Press && event.action != GuiKeyAction::Repeat) {
        return false;
    }
    const int step = 24;
    switch (event.keyCode) {
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

bool GuiScrollView::OnScroll(const GuiScrollEvent& event)
{
    if (!visible_ || bounds_.h <= 0 || !IsScrollInteractionEnabled(scrollbarMode_)) {
        return false;
    }
    scrollY_ -= static_cast<int>(event.yoffset * 24);
    ClampScroll();
    LayoutContent();
    return true;
}

bool GuiScrollView::ScrollAtPoint(int x, int y, const GuiScrollEvent& event)
{
    if (!visible_ || !bounds_.Contains(x, y) || !IsScrollInteractionEnabled(scrollbarMode_)) {
        return false;
    }
    return OnScroll(event);
}

void GuiScrollView::CollectFocusables(std::vector<GuiWidget*>& out)
{
    if (!visible_ || !enabled_) {
        return;
    }
    content_.CollectFocusables(out);
}

} // namespace cutum
