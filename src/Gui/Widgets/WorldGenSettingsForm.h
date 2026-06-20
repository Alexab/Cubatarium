#pragma once

#include "Gui/Core/GuiTypes.h"
#include "Gui/Layout/GuiLayout.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include <memory>
#include <vector>

namespace cutum
{

struct GuiTheme;
class UGuiPanel;
class UGuiLabel;
class UGuiTextInput;
class UGuiButton;
class UGuiCheckbox;
class UGuiListView;

class UWorldGenSettingsForm
{
public:
  explicit UWorldGenSettingsForm(const GuiTheme *theme);

  void SetSettings(const ProceduralSettings &settings);
  void SetHintText(const std::string &text);
  void SetForNewWorldDefaults();
  ProceduralSettings ReadSettings() const;

  void BuildInto(UGuiPanel &panel);
  int MeasureGridHeight(const GuiRect &area, const GuiGridSpec &spec) const;
  void LayoutGrid(const GuiRect &area, const GuiGridSpec &spec) const;

private:
  void AddWidgetsTo(UGuiPanel &panel);
  void CycleVertical();
  void OnGeneratorSelected(int index);
  void RefreshGeneratorDescription();
  void UpdateFieldVisibility();

  const GuiTheme *Theme;
  ProceduralSettings FormSettings;
  bool Built{false};
  bool ForNewWorldScreen{false};

  UGuiLabel *HintLabel{nullptr};
  UGuiLabel *GeneratorCaption{nullptr};
  UGuiLabel *GeneratorDescLabel{nullptr};
  UGuiListView *GeneratorList{nullptr};
  UGuiLabel *VerticalCaption{nullptr};
  UGuiLabel *SeedLabel{nullptr};
  UGuiLabel *SeaLevelLabel{nullptr};
  UGuiLabel *MaxHeightLabel{nullptr};
  UGuiLabel *FlatYLabel{nullptr};
  UGuiLabel *VegetationDensityLabel{nullptr};
  UGuiLabel *DecorationDensityLabel{nullptr};
  UGuiLabel *StructureDensityLabel{nullptr};
  UGuiLabel *TerrainRoughnessLabel{nullptr};
  UGuiLabel *BiomeForestLabel{nullptr};
  UGuiLabel *BiomeDesertLabel{nullptr};
  UGuiButton *VerticalBtn{nullptr};
  UGuiTextInput *SeedInput{nullptr};
  UGuiTextInput *SeaLevelInput{nullptr};
  UGuiTextInput *MaxHeightInput{nullptr};
  UGuiTextInput *FlatYInput{nullptr};
  UGuiTextInput *VegetationDensityInput{nullptr};
  UGuiTextInput *DecorationDensityInput{nullptr};
  UGuiTextInput *StructureDensityInput{nullptr};
  UGuiTextInput *TerrainRoughnessInput{nullptr};
  UGuiTextInput *BiomeForestInput{nullptr};
  UGuiTextInput *BiomeDesertInput{nullptr};
  UGuiCheckbox *CavesBox{nullptr};
  UGuiCheckbox *TreesBox{nullptr};
  UGuiCheckbox *WaterBox{nullptr};
  UGuiCheckbox *LavaBox{nullptr};
  UGuiCheckbox *FireBox{nullptr};

  std::vector<GuiGridItem> BuildGridItems() const;
};

} // namespace cutum
