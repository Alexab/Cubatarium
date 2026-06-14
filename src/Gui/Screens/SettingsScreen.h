#pragma once

#include "Gui/GuiScreenBase.h"
#include "Gui/GuiTypes.h"
#include "UiSettings.h"
#include "Gui/Layout/GuiLayout.h"
#include <memory>
#include <vector>

namespace cutum {

class IGuiMenuHost;
class UGuiPanel;
class UGuiWindow;
class UGuiDialogFrame;
class UWorldGenSettingsForm;
class UGuiTextInput;
class UGuiCheckbox;
class UGuiLabel;
class UGuiButton;

class USettingsScreen : public UGuiScreenBase {
public:
    explicit USettingsScreen(IGuiMenuHost* host);

    void Build(UGuiContext& ctx) override;
    void OnViewportChanged(int width, int height) override;

private:
    void Relayout();
    void ShowTab(int tab);
    void OnSave();
    int MeasureAppPageHeight(const GuiRect& area) const;
    void LayoutAppPage(const GuiRect& area) const;
    void LayoutHotbarCountControls(const GuiGridSpec& spec) const;
    std::vector<GuiGridItem> BuildAppGridItems(const GuiGridSpec& spec) const;
    int MeasureWorldPageHeight(const GuiRect& area) const;
    void LayoutWorldPage(const GuiRect& area) const;

    IGuiMenuHost* host_{nullptr};
    UGuiWindow* Window{nullptr};
    UGuiDialogFrame* dialogFrame_{nullptr};
    UGuiPanel* appPanel_{nullptr};
    UGuiPanel* worldPanel_{nullptr};
    std::unique_ptr<UWorldGenSettingsForm> worldForm_;

    UGuiLabel* defaultUserLabel_{nullptr};
    UGuiLabel* defaultWorldLabel_{nullptr};
    UGuiLabel* renderDistLabel_{nullptr};
    UGuiLabel* consoleKeyLabel_{nullptr};
    UGuiLabel* paletteKeyLabel_{nullptr};
    UGuiLabel* hotbarCountLabel_{nullptr};
    UGuiLabel* hotbarCountValueLabel_{nullptr};
    UGuiLabel* controlSchemeLabel_{nullptr};
    UGuiButton* controlSchemeButton_{nullptr};
    ControlScheme controlScheme_{ControlScheme::Classic};

    UGuiTextInput* defaultUserInput_{nullptr};
    UGuiTextInput* defaultWorldInput_{nullptr};
    UGuiTextInput* renderDistInput_{nullptr};
    UGuiCheckbox* streamingBox_{nullptr};
    UGuiCheckbox* stepUpBox_{nullptr};
    UGuiCheckbox* greedyBox_{nullptr};
    UGuiCheckbox* faceQuadsBox_{nullptr};
    UGuiCheckbox* frustumBox_{nullptr};
    UGuiCheckbox* batchCacheBox_{nullptr};
    UGuiCheckbox* legacyHudBox_{nullptr};
    UGuiTextInput* consoleKeyInput_{nullptr};
    UGuiTextInput* paletteKeyInput_{nullptr};
    UGuiButton* hotbarMinusButton_{nullptr};
    UGuiButton* hotbarPlusButton_{nullptr};
    int hotbarCount_{1};
};

} // namespace cutum
