#ifndef MAIN_MENU_SCREEN_H
#define MAIN_MENU_SCREEN_H

#include "Gui/GuiScreenBase.h"
#include <memory>
#include <vector>

namespace cutum {

class IGuiGameActions;
class GuiButton;
class GuiLabel;
struct GuiTheme;

class MainMenuScreen : public GuiScreenBase {
public:
    explicit MainMenuScreen(IGuiGameActions* actions);

    void Build(GuiContext& ctx) override;
    void OnViewportChanged(int width, int height) override;
    bool BlocksGameInput() const override { return true; }

private:
    void Relayout();

    IGuiGameActions* actions_{nullptr};
    GuiLabel* title_{nullptr};
    GuiLabel* versionLabel_{nullptr};
    GuiLabel* hotbarCountLabel_{nullptr};
    GuiButton* hotbarMinusButton_{nullptr};
    GuiButton* hotbarPlusButton_{nullptr};
    std::vector<GuiButton*> buttons_;
};

} // namespace cutum

#endif
