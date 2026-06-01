#ifndef IN_GAME_HUD_SCREEN_H
#define IN_GAME_HUD_SCREEN_H

#include "Gui/GuiScreenBase.h"
#include <memory>
#include <vector>

namespace cutum {

class IHotbarViewModel;
class GuiSlot;
class GuiPanel;
class GuiLabel;
struct GuiTheme;

class InGameHudScreen : public GuiScreenBase {
public:
    InGameHudScreen(IHotbarViewModel* hotbar, const GuiTheme* theme);

    void Update(double dt) override;
    void Build(GuiContext& ctx) override;

private:
    void RebuildHotbar();

    IHotbarViewModel* hotbar_{nullptr};
    const GuiTheme* theme_;
    GuiPanel* rootPanel_{nullptr};
    std::vector<GuiSlot*> blockSlots_;
    std::vector<GuiSlot*> prefabSlots_;
    GuiLabel* activeLabel_{nullptr};
};

} // namespace cutum

#endif
