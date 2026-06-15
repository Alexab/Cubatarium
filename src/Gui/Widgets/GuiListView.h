#ifndef GUI_LIST_VIEW_H
#define GUI_LIST_VIEW_H

#include "GuiWidget.h"
#include <functional>
#include <string>
#include <vector>

namespace cutum
{

struct GuiTheme;

class UGuiListView : public UGuiWidget
{
public:
  UGuiListView(const GuiTheme *theme);

  void SetItems(std::vector<std::string> items);
  void SetSelectedIndex(int index);
  int GetSelectedIndex() const { return selectedIndex_; }
  void SetOnSelectionChanged(std::function<void(int)> handler);
  void SetAcceptKeyNavigation(bool enabled) { acceptKeyNavigation_ = enabled; }
  void ScrollToEnd();

  bool CanFocus() const override;
  void RevealFocused();

  void Draw(UGuiRenderer &renderer) override;
  bool OnMouseDown(const GuiMouseEvent &event) override;
  bool OnMouseUp(const GuiMouseEvent &event) override;
  bool OnMouseMove(const GuiMouseEvent &event) override;
  bool OnKey(const GuiKeyEvent &event) override;
  bool OnScroll(const GuiScrollEvent &event) override;
  bool ScrollAtPoint(int x, int y, const GuiScrollEvent &event) override;

private:
  int ContentHeight() const;
  int MaxScrollY() const;
  void ClampScroll();
  GuiRect ScrollbarTrackRect() const;
  GuiRect ScrollbarThumbRect() const;
  GuiRect ListAreaRect() const;
  void DrawScrollbar(UGuiRenderer &renderer);
  void EnsureSelectedVisible();
  bool SelectIndex(int index);
  bool HandleKeyNavigation(const GuiKeyEvent &event);

  const GuiTheme *theme_;
  std::vector<std::string> items_;
  int selectedIndex_{-1};
  int scrollOffsetPx_{0};
  int rowHeight_{20};
  std::function<void(int)> onSelectionChanged_;
  bool acceptKeyNavigation_{true};
  bool dragActive_{false};
  bool dragMoved_{false};
  int dragStartY_{0};
  int dragStartScroll_{0};
  int pendingSelectIndex_{-1};
  static constexpr int kScrollbarWidth = 10;
};

} // namespace cutum

#endif
