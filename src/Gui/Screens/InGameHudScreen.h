#ifndef IN_GAME_HUD_SCREEN_H
#define IN_GAME_HUD_SCREEN_H

#include "Gui/GuiScreenBase.h"
#include <memory>
#include <vector>

namespace cutum {

class IHotbarViewModel;
class IGuiIconSource;
class GuiSlot;
class GuiPanel;
class GuiLabel;
struct GuiTheme;

class InGameHudScreen : public GuiScreenBase {
public:
    InGameHudScreen(IHotbarViewModel* hotbar, const GuiTheme* theme, IGuiIconSource* icons);

    void Update(double dt) override;
    void Build(GuiContext& ctx) override;
    void OnViewportChanged(int width, int height) override;
    void SetPointerPosition(int x, int y);
    /// Обновить текстуры слотов; вызывать после отрисовки мира (FBO-иконки prefab).
    void SyncSlotIcons();

private:
    void EnsureHotbarWidgets();
    void LayoutHotbar();
    void UpdateSlotData();
    void UpdateTooltips();

    IHotbarViewModel* hotbar_{nullptr};
    IGuiIconSource* icons_{nullptr};
    const GuiTheme* theme_;
    GuiPanel* rootPanel_{nullptr};
    std::vector<GuiSlot*> blockSlots_;
    std::vector<GuiSlot*> prefabSlots_;
    GuiLabel* blockTooltip_{nullptr};
    GuiLabel* prefabTooltip_{nullptr};
    int pointerX_{-1};
    int pointerY_{-1};
    bool hotbarBuilt_{false};
};

} // namespace cutum

#endif
