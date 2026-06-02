#pragma once

#include "ProceduralSettings.h"
#include "Gui/GuiTypes.h"
#include "Gui/Layout/GuiLayout.h"
#include <memory>
#include <vector>

namespace cutum {

struct GuiTheme;
class GuiPanel;
class GuiLabel;
class GuiTextInput;
class GuiButton;
class GuiCheckbox;

class WorldGenSettingsForm {
public:
    explicit WorldGenSettingsForm(const GuiTheme* theme);

    void SetSettings(const ProceduralSettings& settings);
    ProceduralSettings ReadSettings() const;

    void BuildInto(GuiPanel& panel);
    int MeasureGridHeight(const GuiRect& area, const GuiGridSpec& spec) const;
    void LayoutGrid(const GuiRect& area, const GuiGridSpec& spec) const;

private:
    void AddWidgetsTo(GuiPanel& panel);
    void CycleGenerator();
    void CycleVertical();

    const GuiTheme* theme_;
    ProceduralSettings settings_;
    bool built_{false};

    GuiLabel* hintLabel_{nullptr};
    GuiLabel* generatorLabel_{nullptr};
    GuiLabel* verticalLabel_{nullptr};
    GuiLabel* seedLabel_{nullptr};
    GuiLabel* seaLevelLabel_{nullptr};
    GuiLabel* maxHeightLabel_{nullptr};
    GuiLabel* flatYLabel_{nullptr};
    GuiButton* generatorBtn_{nullptr};
    GuiButton* verticalBtn_{nullptr};
    GuiTextInput* seedInput_{nullptr};
    GuiTextInput* seaLevelInput_{nullptr};
    GuiTextInput* maxHeightInput_{nullptr};
    GuiTextInput* flatYInput_{nullptr};
    GuiCheckbox* cavesBox_{nullptr};
    GuiCheckbox* treesBox_{nullptr};
    GuiCheckbox* waterBox_{nullptr};
    GuiCheckbox* lavaBox_{nullptr};
    GuiCheckbox* fireBox_{nullptr};

    std::vector<GuiGridItem> BuildGridItems() const;
};

} // namespace cutum
