#pragma once

#include "ProceduralSettings.h"
#include "Gui/GuiTypes.h"
#include "Gui/Layout/GuiLayout.h"
#include <memory>
#include <vector>

namespace cutum {

struct GuiTheme;
class UGuiPanel;
class UGuiLabel;
class UGuiTextInput;
class UGuiButton;
class UGuiCheckbox;

class UWorldGenSettingsForm {
public:
    explicit UWorldGenSettingsForm(const GuiTheme* theme);

    void SetSettings(const ProceduralSettings& settings);
    ProceduralSettings ReadSettings() const;

    void BuildInto(UGuiPanel& panel);
    int MeasureGridHeight(const GuiRect& area, const GuiGridSpec& spec) const;
    void LayoutGrid(const GuiRect& area, const GuiGridSpec& spec) const;

private:
    void AddWidgetsTo(UGuiPanel& panel);
    void CycleGenerator();
    void CycleVertical();

    const GuiTheme* theme_;
    ProceduralSettings settings_;
    bool built_{false};

    UGuiLabel* hintLabel_{nullptr};
    UGuiLabel* generatorLabel_{nullptr};
    UGuiLabel* verticalLabel_{nullptr};
    UGuiLabel* seedLabel_{nullptr};
    UGuiLabel* seaLevelLabel_{nullptr};
    UGuiLabel* maxHeightLabel_{nullptr};
    UGuiLabel* flatYLabel_{nullptr};
    UGuiButton* generatorBtn_{nullptr};
    UGuiButton* verticalBtn_{nullptr};
    UGuiTextInput* seedInput_{nullptr};
    UGuiTextInput* seaLevelInput_{nullptr};
    UGuiTextInput* maxHeightInput_{nullptr};
    UGuiTextInput* flatYInput_{nullptr};
    UGuiCheckbox* cavesBox_{nullptr};
    UGuiCheckbox* treesBox_{nullptr};
    UGuiCheckbox* waterBox_{nullptr};
    UGuiCheckbox* lavaBox_{nullptr};
    UGuiCheckbox* fireBox_{nullptr};

    std::vector<GuiGridItem> BuildGridItems() const;
};

} // namespace cutum
