#ifndef GUI_DIALOG_FRAME_H
#define GUI_DIALOG_FRAME_H

#include "Gui/Widgets/GuiScrollView.h"
#include "Gui/Widgets/GuiWidget.h"
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

  static constexpr int kTabGap = 4;

  explicit UGuiDialogFrame(const GuiTheme *theme);

  UGuiTabBar *CreateTabBar(const std::vector<std::string> &labels,
                           std::function<void(int)> onTabChanged);
  UGuiPanel &AddScrollPage();
  UGuiWidget &SetFixedBody(std::unique_ptr<UGuiWidget> body);
  UGuiButton &AddFooterButton(std::unique_ptr<UGuiButton> Button);

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

  const GuiTheme *Theme;
  UGuiTabBar *TabBar{nullptr};
  UGuiScrollView *Scroll{nullptr};
  UGuiWidget *FixedBody{nullptr};
  std::vector<UGuiPanel *> ScrollPages;
  std::vector<PageMeasureFn> ScrollPageMeasureFns;
  std::vector<PageLayoutFn> ScrollPageLayoutFns;
  std::vector<UGuiButton *> FooterButtons;
  int ActivePage{0};
  GuiScrollbarMode ScrollbarMode{GuiScrollbarMode::Auto};
};

} // namespace cutum

#endif
