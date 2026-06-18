#ifndef GUI_CHECK_LIST_H
#define GUI_CHECK_LIST_H

#include "Gui/Widgets/GuiWidget.h"
#include <functional>
#include <string>
#include <vector>

namespace cutum
{

struct GuiTheme;

struct GuiCheckListItem
{
  std::string Id;
  std::string Label;
  bool Checked{false};
};

class UGuiCheckList : public UGuiWidget
{
public:
  explicit UGuiCheckList(const GuiTheme *theme);

  void SetItems(std::vector<GuiCheckListItem> items);
  void SetCheckedIds(const std::vector<std::string> &ids);
  std::vector<std::string> GetCheckedIds() const;
  void SetOnChanged(std::function<void()> handler);
  bool MoveFocusedItem(int delta);

  bool CanFocus() const override;
  bool Activate() override;
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
  void EnsureFocusedVisible();
  bool FocusIndex(int index);
  bool ToggleIndex(int index);
  bool HandleKeyNavigation(const GuiKeyEvent &event);
  void NotifyChanged();

  const GuiTheme *Theme;
  std::vector<GuiCheckListItem> Items;
  int FocusedIndex{-1};
  int ScrollOffsetPx{0};
  int RowHeight{20};
  std::function<void()> OnChanged;
  bool DragActive{false};
  bool DragMoved{false};
  bool ReorderDrag{false};
  int DragStartY{0};
  int DragStartScroll{0};
  int PendingToggleIndex{-1};
  static constexpr int kScrollbarWidth = 10;
};

} // namespace cutum

#endif
