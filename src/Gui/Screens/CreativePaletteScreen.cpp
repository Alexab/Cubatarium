#include "CreativePaletteScreen.h"
#include "Gui/GuiContext.h"
#include "Gui/Interfaces/IContentCatalog.h"
#include "Gui/Interfaces/IHotbarViewModel.h"
#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Widgets/GuiScrollView.h"
#include "Gui/Widgets/GuiSlot.h"
#include "Gui/Widgets/GuiTabBar.h"

namespace cutum {

CreativePaletteScreen::CreativePaletteScreen(IContentCatalog* catalog, IHotbarViewModel* hotbar)
    : catalog_(catalog)
    , hotbar_(hotbar)
{
}

void CreativePaletteScreen::SetVisible(bool visible)
{
    visible_ = visible;
    if (root_) {
        root_->SetVisible(visible);
    }
}

void CreativePaletteScreen::Toggle()
{
    SetVisible(!visible_);
}

void CreativePaletteScreen::Build(GuiContext& ctx)
{
    theme_ = &ctx.GetTheme();
    auto panel = std::make_unique<GuiPanel>(theme_);
    panel->SetVisible(false);
    panel_ = panel.get();

    auto mainTabs = std::make_unique<GuiTabBar>(theme_);
    mainTabs->SetTabs({"Blocks", "Objects"});
    mainTabs_ = mainTabs.get();
    mainTabs->SetOnTabChanged([this](int tab) {
        kind_ = tab == 0 ? ContentKind::Block : ContentKind::Object;
        if (catalog_) {
            const auto types = catalog_->GetTypeIds(kind_);
            activeTypeId_ = types.empty() ? "misc" : types.front();
        }
        built_ = false;
    });

    auto subTabs = std::make_unique<GuiTabBar>(theme_);
    subTabs_ = subTabs.get();
    subTabs->SetOnTabChanged([this](int tab) {
        if (catalog_) {
            const auto types = catalog_->GetTypeIds(kind_);
            if (tab >= 0 && tab < static_cast<int>(types.size())) {
                activeTypeId_ = types[static_cast<size_t>(tab)];
            }
        }
        built_ = false;
    });

    auto scroll = std::make_unique<GuiScrollView>(theme_);
    scroll_ = scroll.get();

    panel->AddChild(std::move(mainTabs));
    panel->AddChild(std::move(subTabs));
    panel->AddChild(std::move(scroll));
    root_ = std::move(panel);
    kind_ = ContentKind::Block;
    activeTypeId_ = "misc";
    built_ = false;
}

void CreativePaletteScreen::Update(double /*dt*/)
{
    if (!visible_ || !panel_ || !catalog_ || !theme_) {
        return;
    }
    const int w = 1280;
    const int h = 720;
    const int panelW = w * 35 / 100;
    panel_->SetBounds({w - panelW, 0, panelW, h});

    if (mainTabs_) {
        mainTabs_->SetBounds({8, 8, panelW - 16, 28});
    }
    if (subTabs_) {
        subTabs_->SetBounds({8, 40, panelW - 16, 28});
        const auto types = catalog_->GetTypeIds(kind_);
        std::vector<std::string> labels;
        for (const auto& id : types) {
            labels.push_back(catalog_->GetTypeDisplayName(id));
        }
        if (labels.empty()) {
            labels.push_back("Misc");
        }
        subTabs_->SetTabs(labels);
        if (activeTypeId_.empty() && !types.empty()) {
            activeTypeId_ = types.front();
        }
    }
    if (scroll_) {
        scroll_->SetBounds({8, 76, panelW - 16, h - 84});
    }

    if (!built_) {
        RebuildGrid();
    }
}

void CreativePaletteScreen::RebuildGrid()
{
    if (!scroll_ || !catalog_ || !hotbar_ || !theme_) {
        return;
    }
    while (!scroll_->GetChildren().empty()) {
        // remove old grid by rebuilding scroll content - not supported, skip
        break;
    }
    auto grid = std::make_unique<GuiPanel>(theme_);
    const auto entries =
        catalog_->GetEntries(kind_, activeTypeId_.empty() ? "misc" : activeTypeId_);
    const int slotSize = theme_->hotbarSlotSize;
    const int cols = 4;
    int x = 0;
    int y = 0;
    for (size_t i = 0; i < entries.size(); ++i) {
        auto slot = std::make_unique<GuiSlot>(theme_, slotSize);
        slot->SetBounds({x, y, slotSize, slotSize});
        const std::string entryId = entries[i].id;
        slot->SetLabel(entries[i].displayName);
        slot->SetOnClick([this, entryId]() {
            if (!hotbar_) {
                return;
            }
            if (kind_ == ContentKind::Block) {
                const auto slots = hotbar_->GetBlockSlots();
                for (size_t si = 0; si < slots.size(); ++si) {
                    if (slots[si].id == entryId) {
                        hotbar_->SelectBlockSlot(si);
                        return;
                    }
                }
            } else {
                const auto slots = hotbar_->GetPrefabSlots();
                for (size_t si = 0; si < slots.size(); ++si) {
                    if (slots[si].id == entryId) {
                        hotbar_->SelectPrefabSlot(si);
                        return;
                    }
                }
            }
        });
        grid->AddChild(std::move(slot));
        x += slotSize + theme_->hotbarSlotGap;
        if ((i + 1) % static_cast<size_t>(cols) == 0) {
            x = 0;
            y += slotSize + theme_->hotbarSlotGap;
        }
    }
    grid->SetBounds({0, 0, scroll_->GetBounds().w, y + slotSize + 8});
    scroll_->SetContent(grid.get());
    scroll_->AddChild(std::move(grid));
    built_ = true;
}

} // namespace cutum
