#ifndef GUI_WINDOW_H
#define GUI_WINDOW_H

#include "GuiPanel.h"
#include <string>

namespace cutum {

class GuiWindow : public GuiPanel {
public:
    GuiWindow(const GuiTheme* theme, std::string title);

    GuiRect GetClientArea() const;
    void Draw(GuiRenderer& renderer) override;

private:
    std::string title_;
    static constexpr int kTitleBarHeight = 24;
};

} // namespace cutum

#endif
