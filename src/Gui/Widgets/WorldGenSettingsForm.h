#pragma once

#include "ProceduralSettings.h"
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

private:
    void AddWidgetsTo(GuiPanel& panel);
    void CycleGenerator();
    void CycleVertical();

    const GuiTheme* theme_;
    ProceduralSettings settings_;
    bool built_{false};

    GuiLabel* hintLabel_{nullptr};
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
};

} // namespace cutum
