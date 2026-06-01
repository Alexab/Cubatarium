#ifndef GUI_SCROLL_VIEW_H
#define GUI_SCROLL_VIEW_H

#include "GuiWidget.h"

namespace cutum {

struct GuiTheme;

class GuiScrollView : public GuiWidget {
public:
    explicit GuiScrollView(const GuiTheme* theme);

    void SetContent(GuiWidget* content);
    void Draw(GuiRenderer& renderer) override;
    bool OnScroll(const GuiScrollEvent& event) override;

private:
    const GuiTheme* theme_;
    GuiWidget* content_{nullptr};
    int scrollY_{0};
};

} // namespace cutum

#endif
