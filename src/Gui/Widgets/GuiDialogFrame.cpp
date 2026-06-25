#include "Gui/Widgets/GuiDialogFrame.h"
#include "Gui/Core/GuiTheme.h"
#include "Gui/Layout/GuiLayout.h"
#include "Gui/Widgets/GuiButton.h"
#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Widgets/GuiTabBar.h"

#include <algorithm>

namespace cutum
{

UGuiDialogFrame::UGuiDialogFrame(const GuiTheme *theme) : Theme(theme) {}

UGuiTabBar *
UGuiDialogFrame::CreateTabBar(const std::vector<std::string> &labels,
                              std::function<void(int)> onTabChanged)
{
  auto tabs = std::make_unique<UGuiTabBar>(Theme);
  TabBar = tabs.get();
  TabBar->SetTabs(labels);
  TabBar->SetOnTabChanged(
      [this, onTabChanged = std::move(onTabChanged)](int tab)
      {
        SetActivePage(tab);
        if (onTabChanged)
        {
          onTabChanged(tab);
        }
      });
  AddChild(std::move(tabs));
  return TabBar;
}

UGuiPanel &UGuiDialogFrame::AddScrollPage()
{
  if (!Scroll)
  {
    auto scroll = std::make_unique<UGuiScrollView>(Theme);
    Scroll = scroll.get();
    Scroll->SetScrollbarMode(ScrollbarMode);
    Scroll->SetAfterScrollLayout([this](UGuiScrollView &)
                                 { RelayoutVisiblePageContents(); });
    AddChild(std::move(scroll));
  }
  auto page = std::make_unique<UGuiPanel>(Theme);
  page->SetDrawBackground(false);
  page->SetStackLayout(6, 0);
  UGuiPanel *raw = page.get();
  ScrollPages.push_back(raw);
  ScrollPageMeasureFns.push_back({});
  ScrollPageLayoutFns.push_back({});
  Scroll->Content().AddChild(std::move(page));
  return *raw;
}

UGuiWidget &UGuiDialogFrame::SetFixedBody(std::unique_ptr<UGuiWidget> body)
{
  FixedBody = body.get();
  AddChild(std::move(body));
  return *FixedBody;
}

UGuiButton &UGuiDialogFrame::AddFooterButton(std::unique_ptr<UGuiButton> Button)
{
  UGuiButton *raw = Button.get();
  FooterButtons.push_back(raw);
  AddChild(std::move(Button));
  return *raw;
}

void UGuiDialogFrame::SetActivePage(int index)
{
  if (ScrollPages.empty())
  {
    return;
  }
  ActivePage = std::clamp(index, 0, static_cast<int>(ScrollPages.size()) - 1);
  for (size_t i = 0; i < ScrollPages.size(); ++i)
  {
    ScrollPages[i]->SetVisible(static_cast<int>(i) == ActivePage);
  }
  LayoutFrame();
}

void UGuiDialogFrame::SetScrollPageLayout(size_t pageIndex,
                                          PageMeasureFn measureFn,
                                          PageLayoutFn layoutFn)
{
  if (pageIndex >= ScrollPageMeasureFns.size() ||
      pageIndex >= ScrollPageLayoutFns.size())
  {
    return;
  }
  ScrollPageMeasureFns[pageIndex] = std::move(measureFn);
  ScrollPageLayoutFns[pageIndex] = std::move(layoutFn);
}

void UGuiDialogFrame::SetDrawScrollbar(bool draw)
{
  ScrollbarMode = draw ? GuiScrollbarMode::Auto : GuiScrollbarMode::Hidden;
  if (Scroll)
  {
    Scroll->SetScrollbarMode(ScrollbarMode);
  }
}

void UGuiDialogFrame::SetScrollbarMode(GuiScrollbarMode mode)
{
  ScrollbarMode = mode;
  if (Scroll)
  {
    Scroll->SetScrollbarMode(mode);
  }
}

void UGuiDialogFrame::LayoutScrollPages(const GuiRect &bodyArea)
{
  if (!Scroll)
  {
    return;
  }
  Scroll->SetBounds(bodyArea);
  for (UGuiPanel *page : ScrollPages)
  {
    if (!page || !page->IsVisible())
    {
      continue;
    }
    const auto it = std::find(ScrollPages.begin(), ScrollPages.end(), page);
    const size_t idx = it == ScrollPages.end()
                           ? 0
                           : static_cast<size_t>(it - ScrollPages.begin());
    int pageH = 0;
    if (idx < ScrollPageMeasureFns.size() && ScrollPageMeasureFns[idx])
    {
      pageH = std::max(
          0, ScrollPageMeasureFns[idx]({0, 0, bodyArea.W, bodyArea.H}));
    }
    else
    {
      std::vector<UGuiWidget *> inner;
      for (const auto &child : page->GetChildren())
      {
        inner.push_back(child.get());
      }
      const GuiRect measureArea{0, 0, bodyArea.W, 100000};
      pageH = UGuiLayout::StackVerticalMeasure(measureArea, 6, 0, inner);
    }
    page->SetBounds({0, 0, bodyArea.W, pageH});
  }
  Scroll->LayoutContent(6, 4);
  RelayoutVisiblePageContents();
}

void UGuiDialogFrame::RelayoutVisiblePageContents()
{
  if (Scroll)
  {
    const GuiRect contentBounds = Scroll->Content().GetBounds();
    for (UGuiPanel *page : ScrollPages)
    {
      if (!page || !page->IsVisible())
      {
        continue;
      }
      const GuiRect pageBounds = page->GetBounds();
      page->SetBounds(
          {contentBounds.X, contentBounds.Y, pageBounds.W, pageBounds.H});
    }
  }

  for (UGuiPanel *page : ScrollPages)
  {
    if (!page || !page->IsVisible())
    {
      continue;
    }
    const auto it = std::find(ScrollPages.begin(), ScrollPages.end(), page);
    const size_t idx = it == ScrollPages.end()
                           ? 0
                           : static_cast<size_t>(it - ScrollPages.begin());
    const GuiRect &pageBounds = page->GetBounds();
    if (idx < ScrollPageLayoutFns.size() && ScrollPageLayoutFns[idx])
    {
      ScrollPageLayoutFns[idx](pageBounds);
      continue;
    }
    std::vector<UGuiWidget *> inner;
    for (const auto &child : page->GetChildren())
    {
      inner.push_back(child.get());
    }
    UGuiLayout::StackVertical(pageBounds, 6, 0, inner);
  }
}

void UGuiDialogFrame::LayoutFrame()
{
  if (Bounds.W <= 0 || Bounds.H <= 0)
  {
    return;
  }
  GuiRect area = Bounds;
  int bodyY = area.Y;

  if (TabBar)
  {
    const int tabH = TabBar->GetPreferredHeight();
    TabBar->SetBounds({area.X, bodyY, area.W, tabH});
    bodyY += tabH + kTabGap;
  }

  const int footer_h = Theme ? Theme->FooterHeight : 44;
  const int footerY = area.Y + area.H - footer_h;
  const int bodyH = std::max(0, footerY - bodyY);
  const GuiRect bodyArea{area.X + 4, bodyY, std::max(0, area.W - 8), bodyH};

  if (Scroll)
  {
    LayoutScrollPages(bodyArea);
  }
  else if (FixedBody)
  {
    FixedBody->SetBounds(bodyArea);
  }

  if (!FooterButtons.empty())
  {
    GuiRect footer{area.X + Theme->Padding, footerY + Theme->Padding / 2,
                   area.W - Theme->Padding * 2, footer_h - Theme->Padding};
    std::vector<UGuiWidget *> btns;
    for (UGuiButton *btn : FooterButtons)
    {
      btns.push_back(btn);
    }
    UGuiLayout::StackHorizontal(footer, 12, 0, btns);
  }
}

void UGuiDialogFrame::CollectFocusables(std::vector<UGuiWidget *> &out)
{
  if (!Visible || !Enabled)
  {
    return;
  }
  if (Scroll)
  {
    Scroll->CollectFocusables(out);
  }
  else if (FixedBody)
  {
    FixedBody->CollectFocusables(out);
  }
  for (UGuiButton *btn : FooterButtons)
  {
    if (btn && btn->CanFocus())
    {
      out.push_back(btn);
    }
  }
}

UGuiWidget *UGuiDialogFrame::HitTest(int x, int y)
{
  return UGuiWidget::HitTest(x, y);
}

bool UGuiDialogFrame::OnKey(const GuiKeyEvent &event)
{
  if (FixedBody && FixedBody->OnKey(event))
  {
    return true;
  }
  if (TabBar && TabBar->OnKey(event))
  {
    return true;
  }
  if (Scroll && Scroll->OnKey(event))
  {
    return true;
  }
  for (UGuiButton *btn : FooterButtons)
  {
    if (btn && btn->OnKey(event))
    {
      return true;
    }
  }
  return false;
}

} // namespace cutum
