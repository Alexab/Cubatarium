#ifndef GUI_LIST_VIEW_H
#define GUI_LIST_VIEW_H

#include "Gui/Widgets/GuiWidget.h"
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
  int GetSelectedIndex() const { return SelectedIndex; }
  void SetOnSelectionChanged(std::function<void(int)> handler);
  void SetAcceptKeyNavigation(bool enabled) { AcceptKeyNavigation = enabled; }
  void SetVisibleRowCount(int rows);
  void ScrollToEnd();

  void SetBounds(const GuiRect &bounds) override;
  int GetPreferredHeight() const override;
  void UpdateLayout(const GuiRect &parentClientArea) override;
  bool ConsumesScrollDragAt(int x, int y) const override;

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
  int MinHeight() const;
  void ClampScroll();
  void ApplyMinimumBounds();
  void ApplySelectionScrollPolicy();
  GuiRect ScrollbarTrackRect() const;
  GuiRect ScrollbarThumbRect() const;
  GuiRect ListAreaRect() const;
  void DrawScrollbar(UGuiRenderer &renderer);
  void EnsureSelectedVisible();
  bool SelectIndex(int index);
  bool HandleKeyNavigation(const GuiKeyEvent &event);

  const GuiTheme *Theme;
  std::vector<std::string> Items;
  int SelectedIndex{-1};
  int ScrollOffsetPx{0};
  int RowHeight{20};
  int VisibleRowCount{5};
  std::function<void(int)> OnSelectionChanged;
  bool AcceptKeyNavigation{true};
  bool DragActive{false};
  bool DragMoved{false};
  int DragStartY{0};
  int DragStartScroll{0};
  int PendingSelectIndex{-1};
  bool HasLayoutBounds{false};
  GuiRect LayoutBounds{};
  int ScrollbarWidthPx() const;
  int TouchSlopPx() const;
};

} // namespace cutum

#endif
