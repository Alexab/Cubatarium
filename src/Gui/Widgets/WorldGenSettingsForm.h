#pragma once

#include "Gui/Core/GuiTypes.h"
#include "Gui/Layout/GuiLayout.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include "WorldGen/Core/WorldGenPack.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace cutum
{

struct GuiTheme;
class UGuiPanel;
class UGuiLabel;
class UGuiTextInput;
class UGuiCheckbox;
class UGuiListView;
class UGuiTabBar;
class UGuiSliderField;

/// World generation settings form with two tabs: "Basic" (generator, preset,
/// seed and the handful of options most players change) and "Advanced"
/// (everything else). Ranged values use a slider + numeric box + Reset button;
/// Reset restores the default of the current generator and preset.
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
  void OnPresetSelected(int index);
  void OnTerrainBackendSelected(int index);
  void OnWorldGenPackSelected(int index);
  void OnTabChanged(int tab);
  void RefreshGeneratorDescription();
  void RefreshWorldGenPackDescription();
  void UpdateFieldVisibility();
  void ConfigureRangesAndDefaults();

  UGuiLabel *AddLabel(UGuiPanel &panel, const std::string &text);
  UGuiSliderField *AddSlider(UGuiPanel &panel, const std::string &label,
                             UGuiLabel *&labelOut, float min, float max,
                             float step, int decimals);
  UGuiCheckbox *AddCheckbox(UGuiPanel &panel, const std::string &label,
                            std::function<void(bool)> onChanged);

  const GuiTheme *Theme;
  ProceduralSettings FormSettings;
  bool Built{false};
  bool ForNewWorldScreen{false};
  bool SeedBlank{false};
  int ActiveTab{0};
  std::function<void()> OnLayoutChanged;

  UGuiTabBar *TabBar{nullptr};
  UGuiLabel *HintLabel{nullptr};
  UGuiLabel *GeneratorCaption{nullptr};
  UGuiLabel *GeneratorDescLabel{nullptr};
  UGuiLabel *PresetLabel{nullptr};
  UGuiListView *GeneratorList{nullptr};
  UGuiListView *PresetList{nullptr};
  UGuiLabel *WorldGenPackIdLabel{nullptr};
  UGuiLabel *WorldGenPackDescLabel{nullptr};
  UGuiListView *WorldGenPackList{nullptr};
  std::vector<WorldGenPackInfo> PackInfos;
  UGuiLabel *SeedLabel{nullptr};
  UGuiTextInput *SeedInput{nullptr};
  UGuiLabel *TerrainBackendLabel{nullptr};
  UGuiListView *TerrainBackendList{nullptr};
  UGuiLabel *CaveStyleLabel{nullptr};
  UGuiListView *CaveStyleList{nullptr};

  // Ranged values (slider + box + reset).
  UGuiLabel *SeaLevelLabel{nullptr};
  UGuiLabel *MaxHeightLabel{nullptr};
  UGuiLabel *BedrockTopYLabel{nullptr};
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
  UGuiLabel *RavineRarityLabel{nullptr};
  UGuiLabel *RavineAquaticMaxDepthLabel{nullptr};
  UGuiSliderField *SeaLevelField{nullptr};
  UGuiSliderField *MaxHeightField{nullptr};
  UGuiSliderField *BedrockTopYField{nullptr};
  UGuiSliderField *FlatYField{nullptr};
  UGuiSliderField *VegetationDensityField{nullptr};
  UGuiSliderField *DecorationDensityField{nullptr};
  UGuiSliderField *StructureDensityField{nullptr};
  UGuiSliderField *TerrainRoughnessField{nullptr};
  UGuiSliderField *BiomeForestField{nullptr};
  UGuiSliderField *BiomeDesertField{nullptr};
  UGuiSliderField *BiomePlainsField{nullptr};
  UGuiSliderField *BiomeHillsField{nullptr};
  UGuiSliderField *BiomeTundraField{nullptr};
  UGuiSliderField *BiomeBlendField{nullptr};
  UGuiSliderField *OreDensityField{nullptr};
  UGuiSliderField *TerrainErosionField{nullptr};
  UGuiSliderField *CaveThresholdField{nullptr};
  UGuiSliderField *CaveMinYField{nullptr};
  UGuiSliderField *CaveScaleField{nullptr};
  UGuiSliderField *CaveMaxDepthField{nullptr};
  UGuiSliderField *RavineRarityField{nullptr};
  UGuiSliderField *RavineAquaticMaxDepthField{nullptr};

  UGuiCheckbox *CavesBox{nullptr};
  UGuiCheckbox *RavinesBox{nullptr};
  UGuiCheckbox *RavineFillWaterBox{nullptr};
  UGuiCheckbox *OresBox{nullptr};
  UGuiCheckbox *TreesBox{nullptr};
  UGuiCheckbox *GroundCoverBox{nullptr};
  UGuiCheckbox *DecorationBox{nullptr};
  UGuiCheckbox *StructuresBox{nullptr};
  UGuiCheckbox *WaterBox{nullptr};
  UGuiCheckbox *LavaBox{nullptr};
  UGuiCheckbox *FireBox{nullptr};
  UGuiLabel *TerrainQualityLabel{nullptr};
  UGuiCheckbox *UnifiedHeightFieldBox{nullptr};
  UGuiCheckbox *AnalyticValleysBox{nullptr};
  UGuiCheckbox *MudflowErosionBox{nullptr};
  UGuiCheckbox *DensityRefineParityBox{nullptr};
  UGuiLabel *TerrainQualityHintLabel{nullptr};

  std::vector<GuiGridItem> BuildGridItems() const;
  void WireTerrainQualityHint(UGuiCheckbox &box, const char *description);
};

} // namespace cutum
