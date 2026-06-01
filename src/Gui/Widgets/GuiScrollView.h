#ifndef GUI_SCROLL_VIEW_H
#define GUI_SCROLL_VIEW_H

#include "GuiPanel.h"
#include <functional>

namespace cutum {

struct GuiTheme;

/// Scrollable viewport with clipped content, scrollbar, wheel and keyboard scrolling.
class GuiScrollView : public GuiWidget {
public:
    explicit GuiScrollView(const GuiTheme* theme);

    GuiPanel& Content();
    const GuiPanel& Content() const;

    void LayoutContent(int spacing = 6, int padding = 0);
    int ContentHeight() const { return contentHeight_; }

    void SetScrollY(int y);
    int GetScrollY() const { return scrollY_; }
    int MaxScrollY() const;

    bool ContainsWidget(const GuiWidget* widget) const;
    void EnsureWidgetVisible(const GuiWidget& widget);
    using AfterScrollLayoutFn = std::function<void(GuiScrollView&)>;
    void SetAfterScrollLayout(AfterScrollLayoutFn callback);

    void Draw(GuiRenderer& renderer) override;
    GuiWidget* HitTest(int x, int y) override;
    GuiWidget* HitTestFocusable(int x, int y) override;
    bool OnMouseDown(const GuiMouseEvent& event) override;
    bool OnMouseUp(const GuiMouseEvent& event) override;
    bool OnMouseMove(const GuiMouseEvent& event) override;
    bool OnChar(const GuiCharEvent& event) override;
    bool OnScroll(const GuiScrollEvent& event) override;
    bool OnKey(const GuiKeyEvent& event) override;
    bool ScrollAtPoint(int x, int y, const GuiScrollEvent& event) override;
    void CollectFocusables(std::vector<GuiWidget*>& out) override;

private:
    bool HandleKeyScroll(const GuiKeyEvent& event);
    GuiRect ViewportRect() const;
    GuiRect ScrollbarTrackRect() const;
    GuiRect ScrollbarThumbRect() const;
    void ClampScroll();
    void DrawScrollbar(GuiRenderer& renderer);

    const GuiTheme* theme_;
    GuiPanel content_;
    int scrollY_{0};
    int contentHeight_{0};
    int layoutSpacing_{6};
    int layoutPadding_{4};
    AfterScrollLayoutFn afterScrollLayout_;
    static constexpr int kScrollbarWidth = 10;
};

} // namespace cutum

#endif
