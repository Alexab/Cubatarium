#ifndef MAIN_MENU_SCREEN_H
#define MAIN_MENU_SCREEN_H

#include "Gui/GuiScreenBase.h"
#include <memory>

namespace cutum {

class IGuiGameActions;
struct GuiTheme;

class MainMenuScreen : public GuiScreenBase {
public:
    explicit MainMenuScreen(IGuiGameActions* actions);

    void Build(GuiContext& ctx) override;
    bool BlocksGameInput() const override { return true; }

private:
    IGuiGameActions* actions_{nullptr};
};

} // namespace cutum

#endif
