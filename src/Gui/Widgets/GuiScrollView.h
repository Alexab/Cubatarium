#ifndef GUI_SCROLL_VIEW_H
#define GUI_SCROLL_VIEW_H

#include "GuiPanel.h"
#include <functional>

namespace cutum {

struct GuiTheme;

enum class GuiScrollbarMode {
    Auto,
    Always,
    Hidden
};

/// Scrollable viewport with clipped content, scrollbar, wheel and keyboard scrolling.
class UGuiScrollView : public UGuiWidget {
public:
    explicit UGuiScrollView(const GuiTheme* theme);

    UGuiPanel& Content();
    const UGuiPanel& Content() const;

    void LayoutContent(int spacing = 6, int padding = 0);
    int ContentHeight() const { return contentHeight_; }

    void SetScrollY(int y);
    int GetScrollY() const { return scrollY_; }
    int MaxScrollY() const;

    bool ContainsWidget(const UGuiWidget* widget) const;
    void EnsureWidgetVisible(const UGuiWidget& widget);
    using AfterScrollLayoutFn = std::function<void(UGuiScrollView&)>;
    void SetAfterScrollLayout(AfterScrollLayoutFn callback);
    void SetScrollbarMode(GuiScrollbarMode mode) { scrollbarMode_ = mode; }
    GuiScrollbarMode GetScrollbarMode() const { return scrollbarMode_; }
    void SetDrawScrollbar(bool draw)
    {
        scrollbarMode_ = draw ? GuiScrollbarMode::Auto : GuiScrollbarMode::Hidden;
    }

    void Draw(UGuiRenderer& renderer) override;
    UGuiWidget* HitTest(int x, int y) override;
    UGuiWidget* HitTestFocusable(int x, int y) override;
    bool OnMouseDown(const GuiMouseEvent& event) override;
    bool OnMouseUp(const GuiMouseEvent& event) override;
    bool OnMouseMove(const GuiMouseEvent& event) override;
    bool OnChar(const GuiCharEvent& event) override;
    bool OnScroll(const GuiScrollEvent& event) override;
    bool OnKey(const GuiKeyEvent& event) override;
    bool ScrollAtPoint(int x, int y, const GuiScrollEvent& event) override;
    void CollectFocusables(std::vector<UGuiWidget*>& out) override;

private:
    bool HandleKeyScroll(const GuiKeyEvent& event);
    GuiRect ViewportRect() const;
    GuiRect ScrollbarTrackRect() const;
    GuiRect ScrollbarThumbRect() const;
    void ClampScroll();
    void DrawScrollbar(UGuiRenderer& renderer);

    const GuiTheme* theme_;
    UGuiPanel content_;
    int scrollY_{0};
    int contentHeight_{0};
    int layoutSpacing_{6};
    int layoutPadding_{4};
    GuiScrollbarMode scrollbarMode_{GuiScrollbarMode::Auto};
    AfterScrollLayoutFn afterScrollLayout_;
    static constexpr int kScrollbarWidth = 10;
};

} // namespace cutum

#endif
