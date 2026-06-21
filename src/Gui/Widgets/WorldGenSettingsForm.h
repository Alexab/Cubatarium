#pragma once

#include "Gui/Core/GuiTypes.h"
#include "Gui/Layout/GuiLayout.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include <functional>
#include <memory>
#include <vector>

namespace cutum
{

struct GuiTheme;
class UGuiPanel;
class UGuiLabel;
class UGuiTextInput;
class UGuiCheckbox;
class UGuiListView;

class UWorldGenSettingsForm
{
public:
  explicit UWorldGenSettingsForm(const GuiTheme *theme);

  void SetSettings(const ProceduralSettings &settings);
  void SetHintText(const std::string &text);
  void SetForNewWorldDefaults();
  void SetOnLayoutChanged(std::function<void()> handler);
  ProceduralSettings ReadSettings() const;

  void BuildInto(UGuiPanel &panel);
  int MeasureGridHeight(const GuiRect &area, const GuiGridSpec &spec) const;
  void LayoutGrid(const GuiRect &area, const GuiGridSpec &spec) const;

private:
  void AddWidgetsTo(UGuiPanel &panel);
  void OnGeneratorSelected(int index);
  void RefreshGeneratorDescription();
  void UpdateFieldVisibility();

  const GuiTheme *Theme;
  ProceduralSettings FormSettings;
  bool Built{false};
  bool ForNewWorldScreen{false};
  std::function<void()> OnLayoutChanged;

  UGuiLabel *HintLabel{nullptr};
  UGuiLabel *GeneratorCaption{nullptr};
  UGuiLabel *GeneratorDescLabel{nullptr};
  UGuiListView *GeneratorList{nullptr};
  UGuiLabel *WorldGenPackIdLabel{nullptr};
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
  UGuiLabel *BiomePlainsLabel{nullptr};
  UGuiLabel *BiomeHillsLabel{nullptr};
  UGuiLabel *BiomeTundraLabel{nullptr};
  UGuiLabel *BiomeBlendLabel{nullptr};
  UGuiLabel *OreDensityLabel{nullptr};
  UGuiLabel *TerrainErosionLabel{nullptr};
  UGuiLabel *CaveThresholdLabel{nullptr};
  UGuiLabel *CaveMinYLabel{nullptr};
  UGuiLabel *CaveScaleLabel{nullptr};
  UGuiLabel *CaveMaxDepthLabel{nullptr};
  UGuiLabel *CaveStyleLabel{nullptr};
  UGuiLabel *BedrockTopYLabel{nullptr};
  UGuiTextInput *WorldGenPackIdInput{nullptr};
  UGuiTextInput *SeedInput{nullptr};
  UGuiTextInput *SeaLevelInput{nullptr};
  UGuiTextInput *MaxHeightInput{nullptr};
  UGuiTextInput *BedrockTopYInput{nullptr};
  UGuiTextInput *FlatYInput{nullptr};
  UGuiTextInput *VegetationDensityInput{nullptr};
  UGuiTextInput *DecorationDensityInput{nullptr};
  UGuiTextInput *StructureDensityInput{nullptr};
  UGuiTextInput *TerrainRoughnessInput{nullptr};
  UGuiTextInput *BiomeForestInput{nullptr};
  UGuiTextInput *BiomeDesertInput{nullptr};
  UGuiTextInput *BiomePlainsInput{nullptr};
  UGuiTextInput *BiomeHillsInput{nullptr};
  UGuiTextInput *BiomeTundraInput{nullptr};
  UGuiTextInput *BiomeBlendInput{nullptr};
  UGuiTextInput *OreDensityInput{nullptr};
  UGuiTextInput *TerrainErosionInput{nullptr};
  UGuiTextInput *CaveThresholdInput{nullptr};
  UGuiTextInput *CaveMinYInput{nullptr};
  UGuiTextInput *CaveScaleInput{nullptr};
  UGuiTextInput *CaveMaxDepthInput{nullptr};
  UGuiTextInput *CaveStyleInput{nullptr};
  UGuiCheckbox *CavesBox{nullptr};
  UGuiCheckbox *OresBox{nullptr};
  UGuiCheckbox *TreesBox{nullptr};
  UGuiCheckbox *DecorationBox{nullptr};
  UGuiCheckbox *StructuresBox{nullptr};
  UGuiCheckbox *WaterBox{nullptr};
  UGuiCheckbox *LavaBox{nullptr};
  UGuiCheckbox *FireBox{nullptr};

  std::vector<GuiGridItem> BuildGridItems() const;
};

} // namespace cutum
