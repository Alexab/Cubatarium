#ifndef MAIN_MENU_SCREEN_H
#define MAIN_MENU_SCREEN_H

#include "Gui/GuiScreenBase.h"
#include <memory>
#include <vector>

namespace cutum {

class IGuiGameActions;
class GuiButton;
class GuiLabel;
class GuiPanel;
struct GuiTheme;

class MainMenuScreen : public GuiScreenBase {
public:
    explicit MainMenuScreen(IGuiGameActions* actions);

    void Build(GuiContext& ctx) override;
    void OnViewportChanged(int width, int height) override;
    bool BlocksGameInput() const override { return true; }

    bool IsQuitConfirmationVisible() const { return quitDialogVisible_; }
    void ShowQuitConfirmation(bool visible);

private:
    void Relayout();
    void RelayoutQuitDialog();

    IGuiGameActions* actions_{nullptr};
    GuiLabel* title_{nullptr};
    GuiLabel* versionLabel_{nullptr};
    std::vector<GuiButton*> buttons_;

    GuiPanel* quitBackdrop_{nullptr};
    GuiPanel* quitDialog_{nullptr};
    GuiLabel* quitMessage_{nullptr};
    GuiButton* quitYesButton_{nullptr};
    GuiButton* quitNoButton_{nullptr};
    bool quitDialogVisible_{false};
};

} // namespace cutum

#endif
