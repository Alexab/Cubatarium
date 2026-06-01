#ifndef GUI_LIST_VIEW_H
#define GUI_LIST_VIEW_H

#include "GuiWidget.h"
#include <functional>
#include <string>
#include <vector>

namespace cutum {

struct GuiTheme;

class GuiListView : public GuiWidget {
public:
    GuiListView(const GuiTheme* theme);

    void SetItems(std::vector<std::string> items);
    void SetSelectedIndex(int index);
    int GetSelectedIndex() const { return selectedIndex_; }
    void SetOnSelectionChanged(std::function<void(int)> handler);

    void Draw(GuiRenderer& renderer) override;
    bool OnMouseDown(const GuiMouseEvent& event) override;
    bool OnScroll(const GuiScrollEvent& event) override;

private:
    const GuiTheme* theme_;
    std::vector<std::string> items_;
    int selectedIndex_{-1};
    int scrollOffsetPx_{0};
    int rowHeight_{20};
    std::function<void(int)> onSelectionChanged_;
};

} // namespace cutum

#endif
