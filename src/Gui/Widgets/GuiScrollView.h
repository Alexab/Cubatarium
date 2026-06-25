#ifndef GUI_SCROLL_VIEW_H
#define GUI_SCROLL_VIEW_H

#include "Gui/Widgets/GuiPanel.h"
#include <functional>

namespace cutum
{

struct GuiTheme;

enum class GuiScrollbarMode
{
  Auto,
  Always,
  Hidden
};

/// Scrollable viewport with clipped content, scrollbar, wheel and keyboard
/// scrolling.
class UGuiScrollView : public UGuiWidget
{
public:
  explicit UGuiScrollView(const GuiTheme *theme);

  UGuiPanel &Content();
  const UGuiPanel &Content() const;

  void LayoutContent(int spacing = 6, int Padding = 0);
  int ContentHeight() const { return ScrollContentHeight; }

  void SetScrollY(int y);
  int GetScrollY() const { return ScrollY; }
  int MaxScrollY() const;

  bool ContainsWidget(const UGuiWidget *widget) const;
  void EnsureWidgetVisible(const UGuiWidget &widget);
  using AfterScrollLayoutFn = std::function<void(UGuiScrollView &)>;
  void SetAfterScrollLayout(AfterScrollLayoutFn callback);
  void SetScrollbarMode(GuiScrollbarMode mode) { ScrollbarMode = mode; }
  GuiScrollbarMode GetScrollbarMode() const { return ScrollbarMode; }
  void SetDrawScrollbar(bool draw)
  {
    ScrollbarMode = draw ? GuiScrollbarMode::Auto : GuiScrollbarMode::Hidden;
  }

  void Draw(UGuiRenderer &renderer) override;
  UGuiWidget *HitTest(int x, int y) override;
  UGuiWidget *HitTestFocusable(int x, int y) override;
  bool OnMouseDown(const GuiMouseEvent &event) override;
  bool OnMouseUp(const GuiMouseEvent &event) override;
  bool OnMouseMove(const GuiMouseEvent &event) override;
  bool BeginDeferredTouch(const GuiMouseEvent &event);
  bool OnDeferredMove(const GuiMouseEvent &event);
  bool OnDeferredUp(const GuiMouseEvent &event);
  bool OnChar(const GuiCharEvent &event) override;
  bool OnScroll(const GuiScrollEvent &event) override;
  bool OnKey(const GuiKeyEvent &event) override;
  bool ScrollAtPoint(int x, int y, const GuiScrollEvent &event) override;
  void CollectFocusables(std::vector<UGuiWidget *> &out) override;

private:
  bool HandleKeyScroll(const GuiKeyEvent &event);
  GuiRect ViewportRect() const;
  GuiRect ScrollbarTrackRect() const;
  GuiRect ScrollbarThumbRect() const;
  void ClampScroll();
  void DrawScrollbar(UGuiRenderer &renderer);

  int ScrollbarWidthPx() const;
  int TouchSlopPx() const;

  const GuiTheme *Theme;
  UGuiPanel ContentPanel;
  int ScrollY{0};
  int ScrollContentHeight{0};
  int LayoutSpacing{6};
  int LayoutPadding{4};
  GuiScrollbarMode ScrollbarMode{GuiScrollbarMode::Auto};
  AfterScrollLayoutFn AfterScrollLayout;
  bool DeferredTouchActive{false};
  bool DeferredDragged{false};
  GuiMouseEvent DeferredDown{};
  int DeferredDragStartY{0};
  int DeferredDragStartScroll{0};
};

} // namespace cutum

#endif
