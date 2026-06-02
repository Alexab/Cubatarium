#pragma once

#include "Gui/GuiScreenBase.h"
#include "Gui/GuiTypes.h"
#include <memory>

namespace cutum {

class IGuiMenuHost;
class GuiPanel;
class GuiWindow;
class GuiDialogFrame;
class WorldGenSettingsForm;
class GuiTextInput;
class GuiCheckbox;
class GuiLabel;
class GuiButton;

class SettingsScreen : public GuiScreenBase {
public:
    explicit SettingsScreen(IGuiMenuHost* host);

    void Build(GuiContext& ctx) override;
    void OnViewportChanged(int width, int height) override;

private:
    void Relayout();
    void ShowTab(int tab);
    void OnSave();
    int MeasureAppPageHeight(const GuiRect& area) const;
    void LayoutAppPage(const GuiRect& area) const;
    int MeasureWorldPageHeight(const GuiRect& area) const;
    void LayoutWorldPage(const GuiRect& area) const;

    IGuiMenuHost* host_{nullptr};
    GuiWindow* window_{nullptr};
    GuiDialogFrame* dialogFrame_{nullptr};
    GuiPanel* appPanel_{nullptr};
    GuiPanel* worldPanel_{nullptr};
    std::unique_ptr<WorldGenSettingsForm> worldForm_;

    GuiLabel* defaultUserLabel_{nullptr};
    GuiLabel* defaultWorldLabel_{nullptr};
    GuiLabel* renderDistLabel_{nullptr};
    GuiLabel* consoleKeyLabel_{nullptr};
    GuiLabel* paletteKeyLabel_{nullptr};
    GuiLabel* hotbarCountLabel_{nullptr};
    GuiLabel* hotbarCountValueLabel_{nullptr};

    GuiTextInput* defaultUserInput_{nullptr};
    GuiTextInput* defaultWorldInput_{nullptr};
    GuiTextInput* renderDistInput_{nullptr};
    GuiCheckbox* streamingBox_{nullptr};
    GuiCheckbox* stepUpBox_{nullptr};
    GuiCheckbox* greedyBox_{nullptr};
    GuiCheckbox* faceQuadsBox_{nullptr};
    GuiCheckbox* frustumBox_{nullptr};
    GuiCheckbox* batchCacheBox_{nullptr};
    GuiCheckbox* legacyHudBox_{nullptr};
    GuiTextInput* consoleKeyInput_{nullptr};
    GuiTextInput* paletteKeyInput_{nullptr};
    GuiButton* hotbarMinusButton_{nullptr};
    GuiButton* hotbarPlusButton_{nullptr};
    int hotbarCount_{1};
};

} // namespace cutum
