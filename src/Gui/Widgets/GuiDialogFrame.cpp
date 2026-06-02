#include "GuiDialogFrame.h"
#include "GuiButton.h"
#include "GuiPanel.h"
#include "GuiTabBar.h"
#include "Gui/GuiTheme.h"
#include "Gui/Layout/GuiLayout.h"

#include <algorithm>

namespace cutum {

GuiDialogFrame::GuiDialogFrame(const GuiTheme* theme)
    : theme_(theme)
{
}

GuiTabBar* GuiDialogFrame::CreateTabBar(const std::vector<std::string>& labels,
                                        std::function<void(int)> onTabChanged)
{
    auto tabs = std::make_unique<GuiTabBar>(theme_);
    tabBar_ = tabs.get();
    tabBar_->SetTabs(labels);
    tabBar_->SetOnTabChanged([this, onTabChanged = std::move(onTabChanged)](int tab) {
        SetActivePage(tab);
        if (onTabChanged) {
            onTabChanged(tab);
        }
    });
    AddChild(std::move(tabs));
    return tabBar_;
}

GuiPanel& GuiDialogFrame::AddScrollPage()
{
    if (!scroll_) {
        auto scroll = std::make_unique<GuiScrollView>(theme_);
        scroll_ = scroll.get();
        scroll_->SetScrollbarMode(scrollbarMode_);
        scroll_->SetAfterScrollLayout([this](GuiScrollView&) { RelayoutVisiblePageContents(); });
        AddChild(std::move(scroll));
    }
    auto page = std::make_unique<GuiPanel>(theme_);
    page->SetDrawBackground(false);
    page->SetStackLayout(6, 0);
    GuiPanel* raw = page.get();
    scrollPages_.push_back(raw);
    scrollPageMeasureFns_.push_back({});
    scrollPageLayoutFns_.push_back({});
    scroll_->Content().AddChild(std::move(page));
    return *raw;
}

GuiWidget& GuiDialogFrame::SetFixedBody(std::unique_ptr<GuiWidget> body)
{
    fixedBody_ = body.get();
    AddChild(std::move(body));
    return *fixedBody_;
}

GuiButton& GuiDialogFrame::AddFooterButton(std::unique_ptr<GuiButton> button)
{
    GuiButton* raw = button.get();
    footerButtons_.push_back(raw);
    AddChild(std::move(button));
    return *raw;
}

void GuiDialogFrame::SetActivePage(int index)
{
    if (scrollPages_.empty()) {
        return;
    }
    activePage_ = std::clamp(index, 0, static_cast<int>(scrollPages_.size()) - 1);
    for (size_t i = 0; i < scrollPages_.size(); ++i) {
        scrollPages_[i]->SetVisible(static_cast<int>(i) == activePage_);
    }
    LayoutFrame();
}

void GuiDialogFrame::SetScrollPageLayout(size_t pageIndex, PageMeasureFn measureFn, PageLayoutFn layoutFn)
{
    if (pageIndex >= scrollPageMeasureFns_.size() || pageIndex >= scrollPageLayoutFns_.size()) {
        return;
    }
    scrollPageMeasureFns_[pageIndex] = std::move(measureFn);
    scrollPageLayoutFns_[pageIndex] = std::move(layoutFn);
}

void GuiDialogFrame::SetDrawScrollbar(bool draw)
{
    scrollbarMode_ = draw ? GuiScrollbarMode::Auto : GuiScrollbarMode::Hidden;
    if (scroll_) {
        scroll_->SetScrollbarMode(scrollbarMode_);
    }
}

void GuiDialogFrame::SetScrollbarMode(GuiScrollbarMode mode)
{
    scrollbarMode_ = mode;
    if (scroll_) {
        scroll_->SetScrollbarMode(mode);
    }
}

void GuiDialogFrame::LayoutScrollPages(const GuiRect& bodyArea)
{
    if (!scroll_) {
        return;
    }
    scroll_->SetBounds(bodyArea);
    for (GuiPanel* page : scrollPages_) {
        if (!page || !page->IsVisible()) {
            continue;
        }
        const auto it = std::find(scrollPages_.begin(), scrollPages_.end(), page);
        const size_t idx = it == scrollPages_.end() ? 0 : static_cast<size_t>(it - scrollPages_.begin());
        int pageH = 0;
        if (idx < scrollPageMeasureFns_.size() && scrollPageMeasureFns_[idx]) {
            pageH = std::max(0, scrollPageMeasureFns_[idx]({0, 0, bodyArea.w, bodyArea.h}));
        } else {
            std::vector<GuiWidget*> inner;
            for (const auto& child : page->GetChildren()) {
                inner.push_back(child.get());
            }
            const GuiRect measureArea{0, 0, bodyArea.w, 100000};
            pageH = GuiLayout::StackVerticalMeasure(measureArea, 6, 0, inner);
        }
        page->SetBounds({0, 0, bodyArea.w, pageH});
    }
    scroll_->LayoutContent(6, 4);
    RelayoutVisiblePageContents();
}

void GuiDialogFrame::RelayoutVisiblePageContents()
{
    if (scroll_) {
        const GuiRect contentBounds = scroll_->Content().GetBounds();
        for (GuiPanel* page : scrollPages_) {
            if (!page || !page->IsVisible()) {
                continue;
            }
            const GuiRect pageBounds = page->GetBounds();
            page->SetBounds({contentBounds.x, contentBounds.y, pageBounds.w, pageBounds.h});
        }
    }

    for (GuiPanel* page : scrollPages_) {
        if (!page || !page->IsVisible()) {
            continue;
        }
        const auto it = std::find(scrollPages_.begin(), scrollPages_.end(), page);
        const size_t idx = it == scrollPages_.end() ? 0 : static_cast<size_t>(it - scrollPages_.begin());
        const GuiRect& pageBounds = page->GetBounds();
        if (idx < scrollPageLayoutFns_.size() && scrollPageLayoutFns_[idx]) {
            scrollPageLayoutFns_[idx](pageBounds);
            continue;
        }
        std::vector<GuiWidget*> inner;
        for (const auto& child : page->GetChildren()) {
            inner.push_back(child.get());
        }
        GuiLayout::StackVertical(pageBounds, 6, 0, inner);
    }
}

void GuiDialogFrame::LayoutFrame()
{
    if (bounds_.w <= 0 || bounds_.h <= 0) {
        return;
    }
    GuiRect area = bounds_;
    int bodyY = area.y;

    if (tabBar_) {
        const int tabH = tabBar_->GetPreferredHeight();
        tabBar_->SetBounds({area.x, bodyY, area.w, tabH});
        bodyY += tabH + kTabGap;
    }

    const int footerY = area.y + area.h - kFooterHeight;
    const int bodyH = std::max(0, footerY - bodyY);
    const GuiRect bodyArea{area.x + 4, bodyY, std::max(0, area.w - 8), bodyH};

    if (scroll_) {
        LayoutScrollPages(bodyArea);
    } else if (fixedBody_) {
        fixedBody_->SetBounds(bodyArea);
    }

    if (!footerButtons_.empty()) {
        GuiRect footer{area.x + 8, footerY + 4, area.w - 16, kFooterHeight - 8};
        std::vector<GuiWidget*> btns;
        for (GuiButton* btn : footerButtons_) {
            btns.push_back(btn);
        }
        GuiLayout::StackHorizontal(footer, 12, 0, btns);
    }
}

void GuiDialogFrame::CollectFocusables(std::vector<GuiWidget*>& out)
{
    if (!visible_ || !enabled_) {
        return;
    }
    if (scroll_) {
        scroll_->CollectFocusables(out);
    } else if (fixedBody_) {
        fixedBody_->CollectFocusables(out);
    }
    for (GuiButton* btn : footerButtons_) {
        if (btn && btn->CanFocus()) {
            out.push_back(btn);
        }
    }
}

GuiWidget* GuiDialogFrame::HitTest(int x, int y)
{
    return GuiWidget::HitTest(x, y);
}

bool GuiDialogFrame::OnKey(const GuiKeyEvent& event)
{
    if (fixedBody_ && fixedBody_->OnKey(event)) {
        return true;
    }
    if (tabBar_ && tabBar_->OnKey(event)) {
        return true;
    }
    if (scroll_ && scroll_->OnKey(event)) {
        return true;
    }
    for (GuiButton* btn : footerButtons_) {
        if (btn && btn->OnKey(event)) {
            return true;
        }
    }
    return false;
}

} // namespace cutum
