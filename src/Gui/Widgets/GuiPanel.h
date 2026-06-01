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

protected:
    const GuiTheme* theme_;
};

} // namespace cutum

#endif
