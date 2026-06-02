#ifndef CREATIVE_PALETTE_SCREEN_H
#define CREATIVE_PALETTE_SCREEN_H

#include "Gui/GuiScreenBase.h"
#include "Gui/Interfaces/IContentCatalog.h"
#include <memory>
#include <string>

namespace cutum {

class IContentCatalog;
class IHotbarViewModel;
class IGuiIconSource;
class GuiTabBar;
class GuiScrollView;
class GuiPanel;
class GuiSlot;
struct GuiTheme;

class CreativePaletteScreen : public GuiScreenBase {
public:
    CreativePaletteScreen(IContentCatalog* catalog, IHotbarViewModel* hotbar,
                          IGuiIconSource* icons);

    void OnViewportChanged(int width, int height) override;

    void Build(GuiContext& ctx) override;
    void Update(double dt) override;
    bool BlocksGameInput() const override { return visible_; }

    void SetVisible(bool visible);
    void Toggle();
    void InvalidateGrid() { built_ = false; }

private:
    void RebuildGrid();
    void RelayoutPanel();
    void LayoutGridInScroll();

    IContentCatalog* catalog_{nullptr};
    IHotbarViewModel* hotbar_{nullptr};
    IGuiIconSource* icons_{nullptr};
    GuiPanel* panel_{nullptr};
    GuiTabBar* mainTabs_{nullptr};
    GuiTabBar* subTabs_{nullptr};
    GuiScrollView* scroll_{nullptr};
    std::vector<GuiSlot*> gridSlots_;
    std::string selectedEntryId_;
    ContentKind kind_{ContentKind::Block};
    std::string activeTypeId_;
    const GuiTheme* theme_{nullptr};
    bool visible_{false};
    bool built_{false};
};

} // namespace cutum

#endif
