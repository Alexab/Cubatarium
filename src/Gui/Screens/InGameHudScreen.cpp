#include "InGameHudScreen.h"
#include "Gui/GuiContext.h"
#include "Gui/Interfaces/IHotbarViewModel.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Widgets/GuiSlot.h"

namespace cutum {

InGameHudScreen::InGameHudScreen(IHotbarViewModel* hotbar, const GuiTheme* theme)
    : hotbar_(hotbar)
    , theme_(theme)
{
}

void InGameHudScreen::Build(GuiContext& ctx)
{
    (void)ctx;
    auto panel = std::make_unique<GuiPanel>(theme_);
    panel->SetDrawBackground(false);
    rootPanel_ = panel.get();
    root_ = std::move(panel);
    RebuildHotbar();
}

void InGameHudScreen::RebuildHotbar()
{
    if (!rootPanel_ || !hotbar_ || !theme_) {
        return;
    }
    blockSlots_.clear();
    prefabSlots_.clear();
    while (!rootPanel_->GetChildren().empty()) {
        // children owned by panel - rebuild by recreating root
    }
    // Simpler: build slots each update on existing panel
}

void InGameHudScreen::SetViewportSize(int width, int height)
{
    if (width > 0) {
        viewportW_ = width;
    }
    if (height > 0) {
        viewportH_ = height;
    }
}

void InGameHudScreen::Update(double /*dt*/)
{
    if (!rootPanel_ || !hotbar_ || !theme_) {
        return;
    }
    const int w = viewportW_;
    const int h = viewportH_;
    rootPanel_->SetBounds({0, 0, w, h});

    if (blockSlots_.empty()) {
        const int slotSize = theme_->hotbarSlotSize;
        const int gap = theme_->hotbarSlotGap;
        const int totalW = 10 * slotSize + 9 * gap;
        int x = (w - totalW) / 2;
        const int y = h - slotSize - 24;
        const auto slots = hotbar_->GetBlockSlots();
        for (size_t i = 0; i < 10; ++i) {
            auto slot = std::make_unique<GuiSlot>(theme_, slotSize);
            slot->SetBounds({x, y, slotSize, slotSize});
            const size_t index = i;
            slot->SetOnClick([this, index]() { hotbar_->SelectBlockSlot(index); });
            GuiSlot* ptr = static_cast<GuiSlot*>(rootPanel_->AddChild(std::move(slot)));
            blockSlots_.push_back(ptr);
            x += slotSize + gap;
        }
        const int prefabY = y - slotSize - gap;
        x = (w - totalW) / 2;
        const auto prefabs = hotbar_->GetPrefabSlots();
        for (size_t i = 0; i < 10; ++i) {
            auto slot = std::make_unique<GuiSlot>(theme_, slotSize);
            slot->SetBounds({x, prefabY, slotSize, slotSize});
            const size_t index = i;
            slot->SetOnClick([this, index]() { hotbar_->SelectPrefabSlot(index); });
            GuiSlot* ptr = static_cast<GuiSlot*>(rootPanel_->AddChild(std::move(slot)));
            prefabSlots_.push_back(ptr);
            x += slotSize + gap;
        }
        auto label = std::make_unique<GuiLabel>(theme_, "");
        label->SetBounds({w / 2 - 200, prefabY - 28, 400, 24});
        activeLabel_ = label.get();
        rootPanel_->AddChild(std::move(label));
    }

    const auto blockSlots = hotbar_->GetBlockSlots();
    for (size_t i = 0; i < blockSlots_.size() && i < blockSlots.size(); ++i) {
        blockSlots_[i]->SetLabel(blockSlots[i].label);
        blockSlots_[i]->SetSelected(blockSlots[i].selected);
    }
    const auto prefabSlots = hotbar_->GetPrefabSlots();
    for (size_t i = 0; i < prefabSlots_.size() && i < prefabSlots.size(); ++i) {
        prefabSlots_[i]->SetLabel(prefabSlots[i].label);
        prefabSlots_[i]->SetSelected(prefabSlots[i].selected);
    }
    if (activeLabel_) {
        const size_t bi = hotbar_->GetActiveBlockIndex();
        std::string text = "Block: ";
        if (bi < blockSlots.size()) {
            text += blockSlots[bi].label;
        }
        activeLabel_->SetText(text);
    }
}

} // namespace cutum
