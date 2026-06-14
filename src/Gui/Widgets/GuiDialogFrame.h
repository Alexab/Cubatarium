#ifndef GUI_DIALOG_FRAME_H
#define GUI_DIALOG_FRAME_H

#include "GuiScrollView.h"
#include "GuiWidget.h"
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace cutum
{

struct GuiTheme;
class UGuiTabBar;
class UGuiPanel;
class UGuiButton;

/// Modal dialog body: optional tabs, scrollable or fixed body, footer buttons.
class UGuiDialogFrame : public UGuiWidget
{
public:
  using PageMeasureFn = std::function<int(const GuiRect &)>;
  using PageLayoutFn = std::function<void(const GuiRect &)>;

  static constexpr int kFooterHeight = 44;
  static constexpr int kTabGap = 4;

  explicit UGuiDialogFrame(const GuiTheme *theme);

  UGuiTabBar *CreateTabBar(const std::vector<std::string> &labels,
                           std::function<void(int)> onTabChanged);
  UGuiPanel &AddScrollPage();
  UGuiWidget &SetFixedBody(std::unique_ptr<UGuiWidget> body);
  UGuiButton &AddFooterButton(std::unique_ptr<UGuiButton> button);

  void SetActivePage(int index);
  void SetScrollPageLayout(size_t pageIndex, PageMeasureFn measureFn,
                           PageLayoutFn layoutFn);
  void SetScrollbarMode(GuiScrollbarMode mode);
  void SetDrawScrollbar(bool draw);
  void LayoutFrame();

  void CollectFocusables(std::vector<UGuiWidget *> &out) override;
  UGuiWidget *HitTest(int x, int y) override;
  bool OnKey(const GuiKeyEvent &event) override;

private:
  void LayoutScrollPages(const GuiRect &bodyArea);
  void RelayoutVisiblePageContents();

  const GuiTheme *theme_;
  UGuiTabBar *tabBar_{nullptr};
  UGuiScrollView *scroll_{nullptr};
  UGuiWidget *fixedBody_{nullptr};
  std::vector<UGuiPanel *> scrollPages_;
  std::vector<PageMeasureFn> scrollPageMeasureFns_;
  std::vector<PageLayoutFn> scrollPageLayoutFns_;
  std::vector<UGuiButton *> footerButtons_;
  int activePage_{0};
  GuiScrollbarMode scrollbarMode_{GuiScrollbarMode::Auto};
};

} // namespace cutum

#endif
