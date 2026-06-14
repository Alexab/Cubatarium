#ifndef GUI_WINDOW_H
#define GUI_WINDOW_H

#include "GuiPanel.h"
#include <string>

namespace cutum {

class UGuiWindow : public UGuiPanel {
public:
    UGuiWindow(const GuiTheme* theme, std::string title);

    GuiRect GetClientArea() const;
    void Draw(UGuiRenderer& renderer) override;

private:
    std::string title_;
    static constexpr int kTitleBarHeight = 24;
};

} // namespace cutum

#endif
