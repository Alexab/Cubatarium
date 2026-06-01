#ifndef GUI_PANEL_H
#define GUI_PANEL_H

#include "GuiWidget.h"
#include <glm/glm.hpp>

namespace cutum {

struct GuiTheme;

class GuiPanel : public GuiWidget {
public:
    explicit GuiPanel(const GuiTheme* theme);

    void Draw(GuiRenderer& renderer) override;

    void SetDrawBackground(bool draw) { drawBackground_ = draw; }
    bool GetDrawBackground() const { return drawBackground_; }

protected:
    const GuiTheme* theme_;
    bool drawBackground_{true};
};

} // namespace cutum

#endif
