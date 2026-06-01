#include "InGameHudScreen.h"
#include "Gui/GuiContext.h"
#include "Gui/Interfaces/IGuiIconSource.h"
#include "Gui/Interfaces/IHotbarViewModel.h"
#include "Gui/Layout/GuiLayout.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Widgets/GuiSlot.h"
#include "Gui/Widgets/GuiWidget.h"

namespace cutum {

namespace {

constexpr int kHotbarMarginBottom = 24;
constexpr int kTooltipHeight = 22;

} // namespace

InGameHudScreen::InGameHudScreen(IHotbarViewModel* hotbar, const GuiTheme* theme, IGuiIconSource* icons)
    : hotbar_(hotbar)
    , theme_(theme)
    , icons_(icons)
{
}

void InGameHudScreen::Build(GuiContext& ctx)
{
    (void)ctx;
    auto panel = std::make_unique<GuiPanel>(theme_);
    panel->SetDrawBackground(false);
    rootPanel_ = panel.get();
    root_ = std::move(panel);
    hotbarBuilt_ = false;
}

void InGameHudScreen::OnViewportChanged(int width, int height)
{
    GuiScreenBase::OnViewportChanged(width, height);
    LayoutHotbar();
}

void InGameHudScreen::SetPointerPosition(int x, int y)
{
    pointerX_ = x;
    pointerY_ = y;
}

void InGameHudScreen::EnsureHotbarWidgets()
{
    if (hotbarBuilt_ || !rootPanel_ || !hotbar_ || !theme_) {
        return;
    }

    const int slotSize = theme_->hotbarSlotSize;

    for (size_t i = 0; i < 10; ++i) {
        auto slot = std::make_unique<GuiSlot>(theme_, slotSize);
        const size_t index = i;
        slot->SetOnClick([this, index]() { hotbar_->SelectBlockSlot(index); });
        GuiSlot* ptr = static_cast<GuiSlot*>(rootPanel_->AddChild(std::move(slot)));
        blockSlots_.push_back(ptr);
    }
    for (size_t i = 0; i < 10; ++i) {
        auto slot = std::make_unique<GuiSlot>(theme_, slotSize);
        const size_t index = i;
        slot->SetOnClick([this, index]() { hotbar_->SelectPrefabSlot(index); });
        GuiSlot* ptr = static_cast<GuiSlot*>(rootPanel_->AddChild(std::move(slot)));
        prefabSlots_.push_back(ptr);
    }

    auto blockTip = std::make_unique<GuiLabel>(theme_, "");
    blockTip->SetTextAlign(GuiTextAlign::Center);
    blockTip->SetDrawBackground(true);
    blockTip->SetVisible(false);
    blockTooltip_ = blockTip.get();
    rootPanel_->AddChild(std::move(blockTip));

    auto prefabTip = std::make_unique<GuiLabel>(theme_, "");
    prefabTip->SetTextAlign(GuiTextAlign::Center);
    prefabTip->SetDrawBackground(true);
    prefabTip->SetVisible(false);
    prefabTooltip_ = prefabTip.get();
    rootPanel_->AddChild(std::move(prefabTip));

    hotbarBuilt_ = true;
    LayoutHotbar();
}

void InGameHudScreen::LayoutHotbar()
{
    if (!hotbarBuilt_ || !theme_) {
        return;
    }
    rootPanel_->SetBounds({0, 0, viewportW_, viewportH_});

    const int slotSize = theme_->hotbarSlotSize;
    const int gap = theme_->hotbarSlotGap;
    const auto layout = LayoutHotbarRows(viewportW_, viewportH_, slotSize, gap, kHotbarMarginBottom);

    int x = layout.startX;
    for (GuiSlot* slot : blockSlots_) {
        if (slot) {
            slot->SetBounds({x, layout.blockRowY, slotSize, slotSize});
            x += slotSize + gap;
        }
    }

    x = layout.startX;
    for (GuiSlot* slot : prefabSlots_) {
        if (slot) {
            slot->SetBounds({x, layout.prefabRowY, slotSize, slotSize});
            x += slotSize + gap;
        }
    }

    if (blockTooltip_) {
        blockTooltip_->SetBounds({layout.startX, layout.blockRowY - gap - kTooltipHeight, layout.totalW,
                                  kTooltipHeight});
    }
    if (prefabTooltip_) {
        prefabTooltip_->SetBounds(
            {layout.startX, layout.prefabRowY + slotSize + gap, layout.totalW, kTooltipHeight});
    }
}

void InGameHudScreen::UpdateSlotData()
{
    if (!hotbar_) {
        return;
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
}

void InGameHudScreen::SyncSlotIcons()
{
    if (!hotbar_ || !icons_) {
        return;
    }
    const auto blockSlots = hotbar_->GetBlockSlots();
    for (size_t i = 0; i < blockSlots_.size() && i < blockSlots.size(); ++i) {
        const GLuint tex =
            blockSlots[i].id.empty() ? 0 : icons_->GetBlockIconTexture(blockSlots[i].id);
        blockSlots_[i]->SetIconTexture(tex);
    }
    const auto prefabSlots = hotbar_->GetPrefabSlots();
    for (size_t i = 0; i < prefabSlots_.size() && i < prefabSlots.size(); ++i) {
        const GLuint tex =
            prefabSlots[i].id.empty() ? 0 : icons_->GetPrefabIconTexture(prefabSlots[i].id);
        prefabSlots_[i]->SetIconTexture(tex);
    }
}

void InGameHudScreen::UpdateTooltips()
{
    if (!hotbar_) {
        return;
    }

    const auto blockSlots = hotbar_->GetBlockSlots();
    const auto prefabSlots = hotbar_->GetPrefabSlots();

    auto showBlockTip = [&](const std::string& text) {
        if (blockTooltip_) {
            blockTooltip_->SetText(text);
            blockTooltip_->SetVisible(!text.empty());
        }
    };
    auto showPrefabTip = [&](const std::string& text) {
        if (prefabTooltip_) {
            prefabTooltip_->SetText(text);
            prefabTooltip_->SetVisible(!text.empty());
        }
    };

    showBlockTip("");
    showPrefabTip("");

    if (pointerX_ >= 0 && pointerY_ >= 0 && rootPanel_) {
        if (GuiWidget* hit = rootPanel_->HitTest(pointerX_, pointerY_)) {
            for (size_t i = 0; i < blockSlots_.size(); ++i) {
                if (hit == blockSlots_[i] && i < blockSlots.size() && !blockSlots[i].label.empty()) {
                    showBlockTip(blockSlots[i].label);
                    return;
                }
            }
            for (size_t i = 0; i < prefabSlots_.size(); ++i) {
                if (hit == prefabSlots_[i] && i < prefabSlots.size() &&
                    !prefabSlots[i].label.empty()) {
                    showPrefabTip(prefabSlots[i].label);
                    return;
                }
            }
        }
    }

    const size_t activeBlock = hotbar_->GetActiveBlockIndex();
    if (activeBlock < blockSlots.size() && !blockSlots[activeBlock].label.empty()) {
        showBlockTip(blockSlots[activeBlock].label);
    }
    const size_t activePrefab = hotbar_->GetActivePrefabIndex();
    if (activePrefab < prefabSlots.size() && !prefabSlots[activePrefab].label.empty()) {
        showPrefabTip(prefabSlots[activePrefab].label);
    }
}

void InGameHudScreen::Update(double /*dt*/)
{
    if (!rootPanel_ || !hotbar_ || !theme_) {
        return;
    }
    EnsureHotbarWidgets();
    LayoutHotbar();
    UpdateSlotData();
    UpdateTooltips();
}

} // namespace cutum
