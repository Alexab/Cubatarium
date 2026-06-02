#ifndef IN_GAME_HUD_SCREEN_H
#define IN_GAME_HUD_SCREEN_H

#include "Gui/GuiScreenBase.h"
#include "SlotInteraction.h"
#include <memory>
#include <vector>

namespace cutum {

class GameSession;
class IGuiIconSource;
class GuiSlot;
class GuiPanel;
class GuiLabel;
struct GuiTheme;

class InGameHudScreen : public GuiScreenBase {
public:
    InGameHudScreen(GameSession* session, const GuiTheme* theme, IGuiIconSource* icons);

    bool PickSlot(int x, int y, SlotAddress& out);

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

    GameSession* session_{nullptr};
    IGuiIconSource* icons_{nullptr};
    const GuiTheme* theme_;
    GuiPanel* rootPanel_{nullptr};
    std::vector<GuiSlot*> primarySlots_;
    std::vector<GuiSlot*> secondarySlots_;
    GuiLabel* tooltip_{nullptr};
    int pointerX_{-1};
    int pointerY_{-1};
    bool hotbarBuilt_{false};
};

} // namespace cutum

#endif
