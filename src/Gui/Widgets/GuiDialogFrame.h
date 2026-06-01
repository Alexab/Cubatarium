#ifndef GUI_DIALOG_FRAME_H
#define GUI_DIALOG_FRAME_H

#include "GuiWidget.h"
#include "GuiScrollView.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace cutum {

struct GuiTheme;
class GuiTabBar;
class GuiPanel;
class GuiButton;

/// Modal dialog body: optional tabs, scrollable or fixed body, footer buttons.
class GuiDialogFrame : public GuiWidget {
public:
    static constexpr int kFooterHeight = 44;
    static constexpr int kTabGap = 4;

    explicit GuiDialogFrame(const GuiTheme* theme);

    GuiTabBar* CreateTabBar(const std::vector<std::string>& labels,
                            std::function<void(int)> onTabChanged);
    GuiPanel& AddScrollPage();
    GuiWidget& SetFixedBody(std::unique_ptr<GuiWidget> body);
    GuiButton& AddFooterButton(std::unique_ptr<GuiButton> button);

    void SetActivePage(int index);
    void LayoutFrame();

    void CollectFocusables(std::vector<GuiWidget*>& out) override;
    GuiWidget* HitTest(int x, int y) override;
    bool OnKey(const GuiKeyEvent& event) override;

private:
    void LayoutScrollPages(const GuiRect& bodyArea);
    void RelayoutVisiblePageContents();

    const GuiTheme* theme_;
    GuiTabBar* tabBar_{nullptr};
    GuiScrollView* scroll_{nullptr};
    GuiWidget* fixedBody_{nullptr};
    std::vector<GuiPanel*> scrollPages_;
    std::vector<GuiButton*> footerButtons_;
    int activePage_{0};
};

} // namespace cutum

#endif
