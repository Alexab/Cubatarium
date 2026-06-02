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
constexpr int kSecondaryMarginRight = 16;
constexpr int kSecondaryMarginBottom = 24;

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
        slot->SetOnClick([this, index]() { hotbar_->SelectSlot(0, index); });
        slot->SetCornerHint(index == 0 ? "0" : std::to_string(index));
        GuiSlot* ptr = static_cast<GuiSlot*>(rootPanel_->AddChild(std::move(slot)));
        primarySlots_.push_back(ptr);
    }
    for (size_t i = 0; i < 10; ++i) {
        auto slot = std::make_unique<GuiSlot>(theme_, slotSize);
        const size_t index = i;
        slot->SetOnClick([this, index]() { hotbar_->SelectSlot(1, index); });
        GuiSlot* ptr = static_cast<GuiSlot*>(rootPanel_->AddChild(std::move(slot)));
        secondarySlots_.push_back(ptr);
    }

    auto tip = std::make_unique<GuiLabel>(theme_, "");
    tip->SetTextAlign(GuiTextAlign::Center);
    tip->SetDrawBackground(true);
    tip->SetVisible(false);
    tooltip_ = tip.get();
    rootPanel_->AddChild(std::move(tip));

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
    const int totalW = static_cast<int>(primarySlots_.size()) * slotSize
        + (static_cast<int>(primarySlots_.size()) - 1) * gap;
    const int startX = (viewportW_ - totalW) / 2;
    const int rowY = viewportH_ - kHotbarMarginBottom - slotSize;

    int x = startX;
    for (GuiSlot* slot : primarySlots_) {
        if (slot) {
            slot->SetBounds({x, rowY, slotSize, slotSize});
            x += slotSize + gap;
        }
    }

    const bool showSecondary = hotbar_->GetBarCount() > 1;
    const int secX = viewportW_ - kSecondaryMarginRight - slotSize;
    int secY = viewportH_ - kSecondaryMarginBottom - slotSize;
    for (GuiSlot* slot : secondarySlots_) {
        if (!slot) {
            continue;
        }
        if (showSecondary) {
            slot->SetVisible(true);
            slot->SetBounds({secX, secY, slotSize, slotSize});
            secY -= slotSize + gap;
        } else {
            slot->SetVisible(false);
        }
    }

    if (tooltip_) {
        tooltip_->SetBounds({startX, rowY - gap - kTooltipHeight, totalW,
                                  kTooltipHeight});
    }
}

void InGameHudScreen::UpdateSlotData()
{
    if (!hotbar_) {
        return;
    }
    const auto primary = hotbar_->GetBarSlots(0);
    for (size_t i = 0; i < primarySlots_.size() && i < primary.size(); ++i) {
        primarySlots_[i]->SetLabel(primary[i].label);
        primarySlots_[i]->SetSelected(primary[i].selected);
    }
    const auto secondary = hotbar_->GetBarSlots(1);
    for (size_t i = 0; i < secondarySlots_.size() && i < secondary.size(); ++i) {
        secondarySlots_[i]->SetLabel(secondary[i].label);
        secondarySlots_[i]->SetSelected(secondary[i].selected);
    }
}

void InGameHudScreen::SyncSlotIcons()
{
    if (!hotbar_ || !icons_) {
        return;
    }
    const auto primary = hotbar_->GetBarSlots(0);
    for (size_t i = 0; i < primarySlots_.size() && i < primary.size(); ++i) {
        const GLuint tex =
            primary[i].id.empty()
                ? 0
                : (primary[i].isBlock
                    ? icons_->GetBlockIconTexture(primary[i].id)
                    : icons_->GetPrefabIconTexture(primary[i].id));
        primarySlots_[i]->SetIconTexture(tex);
    }
    const auto secondary = hotbar_->GetBarSlots(1);
    for (size_t i = 0; i < secondarySlots_.size() && i < secondary.size(); ++i) {
        const GLuint tex =
            secondary[i].id.empty()
                ? 0
                : (secondary[i].isBlock
                    ? icons_->GetBlockIconTexture(secondary[i].id)
                    : icons_->GetPrefabIconTexture(secondary[i].id));
        secondarySlots_[i]->SetIconTexture(tex);
    }
}

void InGameHudScreen::UpdateTooltips()
{
    if (!hotbar_) {
        return;
    }

    const auto primary = hotbar_->GetBarSlots(0);
    const auto secondary = hotbar_->GetBarSlots(1);

    auto showTip = [&](const std::string& text) {
        if (tooltip_) {
            tooltip_->SetText(text);
            tooltip_->SetVisible(!text.empty());
        }
    };

    showTip("");

    if (pointerX_ >= 0 && pointerY_ >= 0 && rootPanel_) {
        if (GuiWidget* hit = rootPanel_->HitTest(pointerX_, pointerY_)) {
            for (size_t i = 0; i < primarySlots_.size(); ++i) {
                if (hit == primarySlots_[i] && i < primary.size() && !primary[i].label.empty()) {
                    showTip(primary[i].label);
                    return;
                }
            }
            for (size_t i = 0; i < secondarySlots_.size(); ++i) {
                if (hit == secondarySlots_[i] && i < secondary.size() &&
                    !secondary[i].label.empty()) {
                    showTip(secondary[i].label);
                    return;
                }
            }
        }
    }

    const size_t activePrimary = hotbar_->GetSelectedSlot(0);
    if (activePrimary < primary.size() && !primary[activePrimary].label.empty()) {
        showTip(primary[activePrimary].label);
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
