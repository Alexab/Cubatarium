#include "Gui/Widgets/WorldGenSettingsForm.h"
#include "Gui/Core/GuiTheme.h"
#include "Gui/Widgets/GuiCheckbox.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/GuiListView.h"
#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Widgets/GuiTextInput.h"
#include "Gui/Widgets/GuiWidget.h"
#include "WorldGen/Core/WorldGeneratorDescriptor.h"
#include <algorithm>
#include <sstream>

namespace cutum
{

namespace
{

int ParseIntOr(const std::string &text, int fallback)
{
  try
  {
    return std::stoi(text);
  }
  catch (...)
  {
    return fallback;
  }
}

uint32_t ParseSeedOr(const std::string &text, uint32_t fallback)
{
  try
  {
    return static_cast<uint32_t>(std::stoul(text));
  }
  catch (...)
  {
    return fallback;
  }
}

float ParseFloatOr(const std::string &text, float fallback)
{
  try
  {
    return std::stof(text);
  }
  catch (...)
  {
    return fallback;
  }
}

void SetWidgetVisible(UGuiWidget *widget, bool visible)
{
  if (widget)
  {
    widget->SetVisible(visible);
  }
}

} // namespace

UWorldGenSettingsForm::UWorldGenSettingsForm(const GuiTheme *theme)
    : Theme(theme)
{
}

void UWorldGenSettingsForm::SetHintText(const std::string &text)
{
  if (HintLabel)
  {
    HintLabel->SetText(text);
  }
}

void UWorldGenSettingsForm::SetForNewWorldDefaults()
{
  ForNewWorldScreen = true;
  SetHintText("Choose generator and tuning for this new world.");
}

void UWorldGenSettingsForm::SetOnLayoutChanged(std::function<void()> handler)
{
  OnLayoutChanged = std::move(handler);
}

void UWorldGenSettingsForm::SetSettings(const ProceduralSettings &settings)
{
  FormSettings = settings;
  if (HintLabel && !ForNewWorldScreen)
  {
    HintLabel->SetText("Defaults for the next new worlds.");
  }
  if (GeneratorList)
  {
    GeneratorList->SetSelectedIndex(
        UWorldGeneratorRegistry::IndexOf(FormSettings.Generator));
  }
  if (WorldGenPackIdInput)
  {
    WorldGenPackIdInput->SetText(FormSettings.WorldGenPackId);
  }
  if (SeedInput)
  {
    SeedInput->SetText(std::to_string(FormSettings.Seed));
  }
  if (SeaLevelInput)
  {
    SeaLevelInput->SetText(std::to_string(FormSettings.SeaLevel));
  }
  if (MaxHeightInput)
  {
    MaxHeightInput->SetText(std::to_string(FormSettings.MaxHeight));
  }
  if (BedrockTopYInput)
  {
    BedrockTopYInput->SetText(std::to_string(FormSettings.BedrockTopY));
  }
  if (FlatYInput)
  {
    FlatYInput->SetText(std::to_string(FormSettings.FlatSurfaceY));
  }
  if (VegetationDensityInput)
  {
    VegetationDensityInput->SetText(
        std::to_string(FormSettings.Tuning.vegetationDensity));
  }
  if (DecorationDensityInput)
  {
    DecorationDensityInput->SetText(
        std::to_string(FormSettings.Tuning.decorationDensity));
  }
  if (StructureDensityInput)
  {
    StructureDensityInput->SetText(
        std::to_string(FormSettings.Tuning.structureDensity));
  }
  if (TerrainRoughnessInput)
  {
    TerrainRoughnessInput->SetText(
        std::to_string(FormSettings.Tuning.terrainRoughness));
  }
  if (BiomeForestInput)
  {
    BiomeForestInput->SetText(
        std::to_string(FormSettings.Tuning.biomeForestWeight));
  }
  if (BiomeDesertInput)
  {
    BiomeDesertInput->SetText(
        std::to_string(FormSettings.Tuning.biomeDesertWeight));
  }
  if (BiomePlainsInput)
  {
    BiomePlainsInput->SetText(
        std::to_string(FormSettings.Tuning.biomePlainsWeight));
  }
  if (BiomeHillsInput)
  {
    BiomeHillsInput->SetText(
        std::to_string(FormSettings.Tuning.biomeHillsWeight));
  }
  if (BiomeTundraInput)
  {
    BiomeTundraInput->SetText(
        std::to_string(FormSettings.Tuning.biomeTundraWeight));
  }
  if (BiomeBlendInput)
  {
    BiomeBlendInput->SetText(
        std::to_string(FormSettings.Tuning.biomeBlendRadius));
  }
  if (OreDensityInput)
  {
    OreDensityInput->SetText(std::to_string(FormSettings.Tuning.oreDensity));
  }
  if (TerrainErosionInput)
  {
    TerrainErosionInput->SetText(
        std::to_string(FormSettings.Tuning.terrainErosion));
  }
  if (CaveThresholdInput)
  {
    CaveThresholdInput->SetText(std::to_string(FormSettings.Caves.threshold));
  }
  if (CaveMinYInput)
  {
    CaveMinYInput->SetText(std::to_string(FormSettings.Caves.minY));
  }
  if (CaveScaleInput)
  {
    CaveScaleInput->SetText(std::to_string(FormSettings.Caves.scale));
  }
  if (CaveMaxDepthInput)
  {
    CaveMaxDepthInput->SetText(
        std::to_string(FormSettings.Caves.maxDepthBelowSurface));
  }
  if (CaveStyleInput)
  {
    CaveStyleInput->SetText(CaveStyleToString(FormSettings.Caves.style));
  }
  if (CavesBox)
  {
    CavesBox->SetChecked(FormSettings.EnableCaves);
  }
  if (OresBox)
  {
    OresBox->SetChecked(FormSettings.EnableOres);
  }
  if (TreesBox)
  {
    TreesBox->SetChecked(FormSettings.EnableTrees);
  }
  if (DecorationBox)
  {
    DecorationBox->SetChecked(FormSettings.EnableDecoration);
  }
  if (StructuresBox)
  {
    StructuresBox->SetChecked(FormSettings.EnableStructures);
  }
  if (WaterBox)
  {
    WaterBox->SetChecked(FormSettings.FillWater);
  }
  if (LavaBox)
  {
    LavaBox->SetChecked(FormSettings.FillLava);
  }
  if (FireBox)
  {
    FireBox->SetChecked(FormSettings.FillFire);
  }
  RefreshGeneratorDescription();
  UpdateFieldVisibility();
  if (OnLayoutChanged)
  {
    OnLayoutChanged();
  }
}

ProceduralSettings UWorldGenSettingsForm::ReadSettings() const
{
  ProceduralSettings s = FormSettings;
  if (WorldGenPackIdInput)
  {
    const std::string packId = WorldGenPackIdInput->GetText();
    if (!packId.empty())
    {
      s.WorldGenPackId = packId;
    }
  }
  if (SeedInput)
  {
    s.Seed = ParseSeedOr(SeedInput->GetText(), s.Seed);
  }
  if (SeaLevelInput)
  {
    s.SeaLevel = ParseIntOr(SeaLevelInput->GetText(), s.SeaLevel);
  }
  if (MaxHeightInput)
  {
    s.MaxHeight = ParseIntOr(MaxHeightInput->GetText(), s.MaxHeight);
  }
  if (BedrockTopYInput)
  {
    s.BedrockTopY = ParseIntOr(BedrockTopYInput->GetText(), s.BedrockTopY);
  }
  if (FlatYInput)
  {
    s.FlatSurfaceY = ParseIntOr(FlatYInput->GetText(), s.FlatSurfaceY);
  }
  if (VegetationDensityInput)
  {
    s.Tuning.vegetationDensity =
        ParseFloatOr(VegetationDensityInput->GetText(), s.Tuning.vegetationDensity);
  }
  if (DecorationDensityInput)
  {
    s.Tuning.decorationDensity =
        ParseFloatOr(DecorationDensityInput->GetText(), s.Tuning.decorationDensity);
  }
  if (StructureDensityInput)
  {
    s.Tuning.structureDensity =
        ParseFloatOr(StructureDensityInput->GetText(), s.Tuning.structureDensity);
  }
  if (TerrainRoughnessInput)
  {
    s.Tuning.terrainRoughness =
        ParseFloatOr(TerrainRoughnessInput->GetText(), s.Tuning.terrainRoughness);
  }
  if (BiomeForestInput)
  {
    s.Tuning.biomeForestWeight =
        ParseFloatOr(BiomeForestInput->GetText(), s.Tuning.biomeForestWeight);
  }
  if (BiomeDesertInput)
  {
    s.Tuning.biomeDesertWeight =
        ParseFloatOr(BiomeDesertInput->GetText(), s.Tuning.biomeDesertWeight);
  }
  if (BiomePlainsInput)
  {
    s.Tuning.biomePlainsWeight =
        ParseFloatOr(BiomePlainsInput->GetText(), s.Tuning.biomePlainsWeight);
  }
  if (BiomeHillsInput)
  {
    s.Tuning.biomeHillsWeight =
        ParseFloatOr(BiomeHillsInput->GetText(), s.Tuning.biomeHillsWeight);
  }
  if (BiomeTundraInput)
  {
    s.Tuning.biomeTundraWeight =
        ParseFloatOr(BiomeTundraInput->GetText(), s.Tuning.biomeTundraWeight);
  }
  if (BiomeBlendInput)
  {
    s.Tuning.biomeBlendRadius =
        ParseFloatOr(BiomeBlendInput->GetText(), s.Tuning.biomeBlendRadius);
  }
  if (OreDensityInput)
  {
    s.Tuning.oreDensity =
        ParseFloatOr(OreDensityInput->GetText(), s.Tuning.oreDensity);
  }
  if (TerrainErosionInput)
  {
    s.Tuning.terrainErosion =
        ParseFloatOr(TerrainErosionInput->GetText(), s.Tuning.terrainErosion);
  }
  if (CaveThresholdInput)
  {
    s.Caves.threshold =
        ParseFloatOr(CaveThresholdInput->GetText(), s.Caves.threshold);
  }
  if (CaveMinYInput)
  {
    s.Caves.minY = ParseIntOr(CaveMinYInput->GetText(), s.Caves.minY);
  }
  if (CaveScaleInput)
  {
    s.Caves.scale = ParseFloatOr(CaveScaleInput->GetText(), s.Caves.scale);
  }
  if (CaveMaxDepthInput)
  {
    s.Caves.maxDepthBelowSurface = ParseIntOr(CaveMaxDepthInput->GetText(),
                                              s.Caves.maxDepthBelowSurface);
  }
  if (CaveStyleInput)
  {
    s.Caves.style = CaveStyleFromString(CaveStyleInput->GetText());
  }
  if (CavesBox)
  {
    s.EnableCaves = CavesBox->IsChecked();
  }
  if (OresBox)
  {
    s.EnableOres = OresBox->IsChecked();
  }
  if (TreesBox)
  {
    s.EnableTrees = TreesBox->IsChecked();
  }
  if (DecorationBox)
  {
    s.EnableDecoration = DecorationBox->IsChecked();
  }
  if (StructuresBox)
  {
    s.EnableStructures = StructuresBox->IsChecked();
  }
  if (WaterBox)
  {
    s.FillWater = WaterBox->IsChecked();
  }
  if (LavaBox)
  {
    s.FillLava = LavaBox->IsChecked();
  }
  if (FireBox)
  {
    s.FillFire = FireBox->IsChecked();
  }
  ResolveProceduralDefaults(s);
  return s;
}

void UWorldGenSettingsForm::OnGeneratorSelected(int index)
{
  if (index < 0 || index >= static_cast<int>(UWorldGeneratorRegistry::Count()))
  {
    return;
  }
  const uint32_t seed = FormSettings.Seed;
  FormSettings.Generator =
      UWorldGeneratorRegistry::Get(static_cast<size_t>(index)).Id;
  ResetToGeneratorDefaults(FormSettings);
  FormSettings.Seed = seed;
  SetSettings(FormSettings);
}

void UWorldGenSettingsForm::RefreshGeneratorDescription()
{
  if (!GeneratorDescLabel)
  {
    return;
  }
  if (const WorldGeneratorDescriptor *descriptor =
          UWorldGeneratorRegistry::Find(FormSettings.Generator))
  {
    GeneratorDescLabel->SetText(descriptor->Description);
  }
}

void UWorldGenSettingsForm::UpdateFieldVisibility()
{
  const WorldGeneratorDescriptor *descriptor =
      UWorldGeneratorRegistry::Find(FormSettings.Generator);
  const uint32_t flags = descriptor ? descriptor->FeatureFlags : 0u;

  const bool showFlat = (flags & kFeatureFlatSurfaceY) != 0;
  const bool showCaves = (flags & kFeatureCaves) != 0;
  const bool showTrees = (flags & kFeatureTrees) != 0;
  const bool showFluids = (flags & kFeatureFluids) != 0;
  const bool showTuning = (flags & kFeatureTuning) != 0;
  const bool showBiomeTuning = (flags & kFeatureBiomes) != 0;
  const bool showStructures = (flags & kFeatureStructures) != 0;
  const bool showOres = (flags & kFeatureOres) != 0;

  SetWidgetVisible(FlatYLabel, showFlat);
  SetWidgetVisible(FlatYInput, showFlat);
  SetWidgetVisible(CavesBox, showCaves);
  SetWidgetVisible(TreesBox, showTrees);
  SetWidgetVisible(DecorationBox, showTrees);
  SetWidgetVisible(StructuresBox, showStructures);
  SetWidgetVisible(WaterBox, showFluids);
  SetWidgetVisible(LavaBox, showFluids);
  SetWidgetVisible(FireBox, showFluids);
  SetWidgetVisible(VegetationDensityLabel, showTrees);
  SetWidgetVisible(VegetationDensityInput, showTrees);
  SetWidgetVisible(DecorationDensityLabel, showTrees);
  SetWidgetVisible(DecorationDensityInput, showTrees);
  SetWidgetVisible(StructureDensityLabel, showStructures);
  SetWidgetVisible(StructureDensityInput, showStructures);
  SetWidgetVisible(TerrainRoughnessLabel, showTuning);
  SetWidgetVisible(TerrainRoughnessInput, showTuning);
  SetWidgetVisible(BiomeForestLabel, showBiomeTuning);
  SetWidgetVisible(BiomeForestInput, showBiomeTuning);
  SetWidgetVisible(BiomeDesertLabel, showBiomeTuning);
  SetWidgetVisible(BiomeDesertInput, showBiomeTuning);
  SetWidgetVisible(BiomePlainsLabel, showBiomeTuning);
  SetWidgetVisible(BiomePlainsInput, showBiomeTuning);
  SetWidgetVisible(BiomeHillsLabel, showBiomeTuning);
  SetWidgetVisible(BiomeHillsInput, showBiomeTuning);
  SetWidgetVisible(BiomeTundraLabel, showBiomeTuning);
  SetWidgetVisible(BiomeTundraInput, showBiomeTuning);
  SetWidgetVisible(WorldGenPackIdLabel, showBiomeTuning);
  SetWidgetVisible(WorldGenPackIdInput, showBiomeTuning);
  SetWidgetVisible(BiomeBlendLabel, showBiomeTuning);
  SetWidgetVisible(BiomeBlendInput, showBiomeTuning);
  SetWidgetVisible(TerrainErosionLabel, showBiomeTuning);
  SetWidgetVisible(TerrainErosionInput, showBiomeTuning);
  SetWidgetVisible(OreDensityLabel, showOres);
  SetWidgetVisible(OreDensityInput, showOres);
  SetWidgetVisible(OresBox, showOres);
  SetWidgetVisible(BedrockTopYLabel, showTuning);
  SetWidgetVisible(BedrockTopYInput, showTuning);
  SetWidgetVisible(CaveThresholdLabel, showCaves);
  SetWidgetVisible(CaveThresholdInput, showCaves);
  SetWidgetVisible(CaveMinYLabel, showCaves);
  SetWidgetVisible(CaveMinYInput, showCaves);
  SetWidgetVisible(CaveScaleLabel, showCaves);
  SetWidgetVisible(CaveScaleInput, showCaves);
  SetWidgetVisible(CaveMaxDepthLabel, showCaves);
  SetWidgetVisible(CaveMaxDepthInput, showCaves);
  SetWidgetVisible(CaveStyleLabel, showCaves);
  SetWidgetVisible(CaveStyleInput, showCaves);
}

void UWorldGenSettingsForm::BuildInto(UGuiPanel &panel)
{
  if (!Built)
  {
    AddWidgetsTo(panel);
    Built = true;
  }
  SetSettings(FormSettings);
}

int UWorldGenSettingsForm::MeasureGridHeight(const GuiRect &area,
                                             const GuiGridSpec &spec) const
{
  return UGuiLayout::GridMeasure(area, spec, BuildGridItems());
}

void UWorldGenSettingsForm::LayoutGrid(const GuiRect &area,
                                       const GuiGridSpec &spec) const
{
  UGuiLayout::GridPlace(area, spec, BuildGridItems());
}

void UWorldGenSettingsForm::AddWidgetsTo(UGuiPanel &panel)
{
  auto hint =
      std::make_unique<UGuiLabel>(Theme, "Defaults for the next new worlds.");
  HintLabel = hint.get();
  panel.AddChild(std::move(hint));

  auto genLabel = std::make_unique<UGuiLabel>(Theme, "Generator:");
  GeneratorCaption = genLabel.get();
  panel.AddChild(std::move(genLabel));

  auto genList = std::make_unique<UGuiListView>(Theme);
  GeneratorList = genList.get();
  std::vector<std::string> names;
  for (size_t i = 0; i < UWorldGeneratorRegistry::Count(); ++i)
  {
    names.push_back(UWorldGeneratorRegistry::Get(i).DisplayName);
  }
  genList->SetItems(std::move(names));
  genList->SetVisibleRowCount(6);
  genList->SetSelectedIndex(
      UWorldGeneratorRegistry::IndexOf(FormSettings.Generator));
  genList->SetOnSelectionChanged([this](int index) { OnGeneratorSelected(index); });
  panel.AddChild(std::move(genList));

  auto genDesc = std::make_unique<UGuiLabel>(Theme, "");
  GeneratorDescLabel = genDesc.get();
  panel.AddChild(std::move(genDesc));

  auto seedLabel = std::make_unique<UGuiLabel>(Theme, "World Seed:");
  SeedLabel = seedLabel.get();
  panel.AddChild(std::move(seedLabel));
  auto seedIn = std::make_unique<UGuiTextInput>(Theme);
  SeedInput = seedIn.get();
  seedIn->SetText(std::to_string(FormSettings.Seed));
  panel.AddChild(std::move(seedIn));

  auto seaLabel = std::make_unique<UGuiLabel>(Theme, "Sea level:");
  SeaLevelLabel = seaLabel.get();
  panel.AddChild(std::move(seaLabel));
  auto seaIn = std::make_unique<UGuiTextInput>(Theme);
  SeaLevelInput = seaIn.get();
  seaIn->SetText(std::to_string(FormSettings.SeaLevel));
  panel.AddChild(std::move(seaIn));

  auto maxLabel = std::make_unique<UGuiLabel>(Theme, "Max height:");
  MaxHeightLabel = maxLabel.get();
  panel.AddChild(std::move(maxLabel));
  auto maxIn = std::make_unique<UGuiTextInput>(Theme);
  MaxHeightInput = maxIn.get();
  maxIn->SetText(std::to_string(FormSettings.MaxHeight));
  panel.AddChild(std::move(maxIn));

  auto bedrockLabel =
      std::make_unique<UGuiLabel>(Theme, "Bedrock top Y (0=single layer):");
  BedrockTopYLabel = bedrockLabel.get();
  panel.AddChild(std::move(bedrockLabel));
  auto bedrockIn = std::make_unique<UGuiTextInput>(Theme);
  BedrockTopYInput = bedrockIn.get();
  bedrockIn->SetText(std::to_string(FormSettings.BedrockTopY));
  panel.AddChild(std::move(bedrockIn));

  auto flatLabel = std::make_unique<UGuiLabel>(Theme, "Flat surface Y:");
  FlatYLabel = flatLabel.get();
  panel.AddChild(std::move(flatLabel));
  auto flatIn = std::make_unique<UGuiTextInput>(Theme);
  FlatYInput = flatIn.get();
  flatIn->SetText(std::to_string(FormSettings.FlatSurfaceY));
  panel.AddChild(std::move(flatIn));

  auto vegLabel =
      std::make_unique<UGuiLabel>(Theme, "Vegetation density (0-2):");
  VegetationDensityLabel = vegLabel.get();
  panel.AddChild(std::move(vegLabel));
  auto vegIn = std::make_unique<UGuiTextInput>(Theme);
  VegetationDensityInput = vegIn.get();
  vegIn->SetText(std::to_string(FormSettings.Tuning.vegetationDensity));
  panel.AddChild(std::move(vegIn));

  auto decorLabel =
      std::make_unique<UGuiLabel>(Theme, "Decoration density (0-2):");
  DecorationDensityLabel = decorLabel.get();
  panel.AddChild(std::move(decorLabel));
  auto decorIn = std::make_unique<UGuiTextInput>(Theme);
  DecorationDensityInput = decorIn.get();
  decorIn->SetText(std::to_string(FormSettings.Tuning.decorationDensity));
  panel.AddChild(std::move(decorIn));

  auto structLabel =
      std::make_unique<UGuiLabel>(Theme, "Structure density (0-2):");
  StructureDensityLabel = structLabel.get();
  panel.AddChild(std::move(structLabel));
  auto structIn = std::make_unique<UGuiTextInput>(Theme);
  StructureDensityInput = structIn.get();
  structIn->SetText(std::to_string(FormSettings.Tuning.structureDensity));
  panel.AddChild(std::move(structIn));

  auto roughLabel =
      std::make_unique<UGuiLabel>(Theme, "Terrain roughness (0.25-2):");
  TerrainRoughnessLabel = roughLabel.get();
  panel.AddChild(std::move(roughLabel));
  auto roughIn = std::make_unique<UGuiTextInput>(Theme);
  TerrainRoughnessInput = roughIn.get();
  roughIn->SetText(std::to_string(FormSettings.Tuning.terrainRoughness));
  panel.AddChild(std::move(roughIn));

  auto forestLabel =
      std::make_unique<UGuiLabel>(Theme, "Forest biome weight (0-2):");
  BiomeForestLabel = forestLabel.get();
  panel.AddChild(std::move(forestLabel));
  auto forestIn = std::make_unique<UGuiTextInput>(Theme);
  BiomeForestInput = forestIn.get();
  forestIn->SetText(std::to_string(FormSettings.Tuning.biomeForestWeight));
  panel.AddChild(std::move(forestIn));

  auto desertLabel =
      std::make_unique<UGuiLabel>(Theme, "Desert biome weight (0-2):");
  BiomeDesertLabel = desertLabel.get();
  panel.AddChild(std::move(desertLabel));
  auto desertIn = std::make_unique<UGuiTextInput>(Theme);
  BiomeDesertInput = desertIn.get();
  desertIn->SetText(std::to_string(FormSettings.Tuning.biomeDesertWeight));
  panel.AddChild(std::move(desertIn));

  auto plainsLabel =
      std::make_unique<UGuiLabel>(Theme, "Plains biome weight (0-2):");
  BiomePlainsLabel = plainsLabel.get();
  panel.AddChild(std::move(plainsLabel));
  auto plainsIn = std::make_unique<UGuiTextInput>(Theme);
  BiomePlainsInput = plainsIn.get();
  plainsIn->SetText(std::to_string(FormSettings.Tuning.biomePlainsWeight));
  panel.AddChild(std::move(plainsIn));

  auto hillsLabel =
      std::make_unique<UGuiLabel>(Theme, "Hills biome weight (0-2):");
  BiomeHillsLabel = hillsLabel.get();
  panel.AddChild(std::move(hillsLabel));
  auto hillsIn = std::make_unique<UGuiTextInput>(Theme);
  BiomeHillsInput = hillsIn.get();
  hillsIn->SetText(std::to_string(FormSettings.Tuning.biomeHillsWeight));
  panel.AddChild(std::move(hillsIn));

  auto tundraLabel =
      std::make_unique<UGuiLabel>(Theme, "Tundra biome weight (0-2):");
  BiomeTundraLabel = tundraLabel.get();
  panel.AddChild(std::move(tundraLabel));
  auto tundraIn = std::make_unique<UGuiTextInput>(Theme);
  BiomeTundraInput = tundraIn.get();
  tundraIn->SetText(std::to_string(FormSettings.Tuning.biomeTundraWeight));
  panel.AddChild(std::move(tundraIn));

  auto blendLabel = std::make_unique<UGuiLabel>(Theme, "Biome blend radius:");
  BiomeBlendLabel = blendLabel.get();
  panel.AddChild(std::move(blendLabel));
  auto blendIn = std::make_unique<UGuiTextInput>(Theme);
  BiomeBlendInput = blendIn.get();
  blendIn->SetText(std::to_string(FormSettings.Tuning.biomeBlendRadius));
  panel.AddChild(std::move(blendIn));

  auto erosionLabel = std::make_unique<UGuiLabel>(Theme, "Terrain erosion (0-1):");
  TerrainErosionLabel = erosionLabel.get();
  panel.AddChild(std::move(erosionLabel));
  auto erosionIn = std::make_unique<UGuiTextInput>(Theme);
  TerrainErosionInput = erosionIn.get();
  erosionIn->SetText(std::to_string(FormSettings.Tuning.terrainErosion));
  panel.AddChild(std::move(erosionIn));

  auto oreLabel = std::make_unique<UGuiLabel>(Theme, "Ore density (0-2):");
  OreDensityLabel = oreLabel.get();
  panel.AddChild(std::move(oreLabel));
  auto oreIn = std::make_unique<UGuiTextInput>(Theme);
  OreDensityInput = oreIn.get();
  oreIn->SetText(std::to_string(FormSettings.Tuning.oreDensity));
  panel.AddChild(std::move(oreIn));

  auto caveThrLabel = std::make_unique<UGuiLabel>(Theme, "Cave threshold:");
  CaveThresholdLabel = caveThrLabel.get();
  panel.AddChild(std::move(caveThrLabel));
  auto caveThrIn = std::make_unique<UGuiTextInput>(Theme);
  CaveThresholdInput = caveThrIn.get();
  caveThrIn->SetText(std::to_string(FormSettings.Caves.threshold));
  panel.AddChild(std::move(caveThrIn));

  auto caveMinLabel = std::make_unique<UGuiLabel>(Theme, "Cave min Y:");
  CaveMinYLabel = caveMinLabel.get();
  panel.AddChild(std::move(caveMinLabel));
  auto caveMinIn = std::make_unique<UGuiTextInput>(Theme);
  CaveMinYInput = caveMinIn.get();
  caveMinIn->SetText(std::to_string(FormSettings.Caves.minY));
  panel.AddChild(std::move(caveMinIn));

  auto caveScaleLabel = std::make_unique<UGuiLabel>(Theme, "Cave noise scale:");
  CaveScaleLabel = caveScaleLabel.get();
  panel.AddChild(std::move(caveScaleLabel));
  auto caveScaleIn = std::make_unique<UGuiTextInput>(Theme);
  CaveScaleInput = caveScaleIn.get();
  caveScaleIn->SetText(std::to_string(FormSettings.Caves.scale));
  panel.AddChild(std::move(caveScaleIn));

  auto caveDepthLabel =
      std::make_unique<UGuiLabel>(Theme, "Cave max depth below surface:");
  CaveMaxDepthLabel = caveDepthLabel.get();
  panel.AddChild(std::move(caveDepthLabel));
  auto caveDepthIn = std::make_unique<UGuiTextInput>(Theme);
  CaveMaxDepthInput = caveDepthIn.get();
  caveDepthIn->SetText(std::to_string(FormSettings.Caves.maxDepthBelowSurface));
  panel.AddChild(std::move(caveDepthIn));

  auto caveStyleLabel = std::make_unique<UGuiLabel>(Theme, "Cave style (noise/worm):");
  CaveStyleLabel = caveStyleLabel.get();
  panel.AddChild(std::move(caveStyleLabel));
  auto caveStyleIn = std::make_unique<UGuiTextInput>(Theme);
  CaveStyleInput = caveStyleIn.get();
  caveStyleIn->SetText(CaveStyleToString(FormSettings.Caves.style));
  panel.AddChild(std::move(caveStyleIn));

  auto caves = std::make_unique<UGuiCheckbox>(Theme, "Caves");
  CavesBox = caves.get();
  caves->SetChecked(FormSettings.EnableCaves);
  caves->SetOnChanged([this](bool v) { FormSettings.EnableCaves = v; });
  panel.AddChild(std::move(caves));

  auto ores = std::make_unique<UGuiCheckbox>(Theme, "Ores");
  OresBox = ores.get();
  ores->SetChecked(FormSettings.EnableOres);
  ores->SetOnChanged([this](bool v) { FormSettings.EnableOres = v; });
  panel.AddChild(std::move(ores));

  auto trees = std::make_unique<UGuiCheckbox>(Theme, "Trees");
  TreesBox = trees.get();
  trees->SetChecked(FormSettings.EnableTrees);
  trees->SetOnChanged([this](bool v) { FormSettings.EnableTrees = v; });
  panel.AddChild(std::move(trees));

  auto decoration = std::make_unique<UGuiCheckbox>(Theme, "Decoration");
  DecorationBox = decoration.get();
  decoration->SetChecked(FormSettings.EnableDecoration);
  decoration->SetOnChanged(
      [this](bool v) { FormSettings.EnableDecoration = v; });
  panel.AddChild(std::move(decoration));

  auto structures = std::make_unique<UGuiCheckbox>(Theme, "Structures");
  StructuresBox = structures.get();
  structures->SetChecked(FormSettings.EnableStructures);
  structures->SetOnChanged(
      [this](bool v) { FormSettings.EnableStructures = v; });
  panel.AddChild(std::move(structures));

  auto water = std::make_unique<UGuiCheckbox>(Theme, "Fill water");
  WaterBox = water.get();
  water->SetChecked(FormSettings.FillWater);
  water->SetOnChanged([this](bool v) { FormSettings.FillWater = v; });
  panel.AddChild(std::move(water));

  auto lava = std::make_unique<UGuiCheckbox>(Theme, "Fill lava");
  LavaBox = lava.get();
  lava->SetChecked(FormSettings.FillLava);
  lava->SetOnChanged([this](bool v) { FormSettings.FillLava = v; });
  panel.AddChild(std::move(lava));

  auto fire = std::make_unique<UGuiCheckbox>(Theme, "Fill fire");
  FireBox = fire.get();
  fire->SetChecked(FormSettings.FillFire);
  fire->SetOnChanged([this](bool v) { FormSettings.FillFire = v; });
  panel.AddChild(std::move(fire));

  auto packLabel = std::make_unique<UGuiLabel>(Theme, "Worldgen pack id:");
  WorldGenPackIdLabel = packLabel.get();
  panel.AddChild(std::move(packLabel));
  auto packIn = std::make_unique<UGuiTextInput>(Theme);
  WorldGenPackIdInput = packIn.get();
  packIn->SetText(FormSettings.WorldGenPackId);
  panel.AddChild(std::move(packIn));
}

std::vector<GuiGridItem> UWorldGenSettingsForm::BuildGridItems() const
{
  std::vector<GuiGridItem> items;
  items.push_back({HintLabel, 0, 0, 1, 2, 28});
  items.push_back({GeneratorCaption, 1, 0, 1, 1, 28});
  items.push_back({GeneratorList, 1, 1, 1, 1, 150});
  items.push_back({GeneratorDescLabel, 2, 0, 1, 2, 36});
  items.push_back({SeedLabel, 3, 0, 1, 1, 28});
  items.push_back({SeedInput, 3, 1, 1, 1, 32});
  items.push_back({SeaLevelLabel, 4, 0, 1, 1, 28});
  items.push_back({SeaLevelInput, 4, 1, 1, 1, 32});
  items.push_back({MaxHeightLabel, 5, 0, 1, 1, 28});
  items.push_back({MaxHeightInput, 5, 1, 1, 1, 32});
  items.push_back({BedrockTopYLabel, 6, 0, 1, 1, 28});
  items.push_back({BedrockTopYInput, 6, 1, 1, 1, 32});
  items.push_back({FlatYLabel, 7, 0, 1, 1, 28});
  items.push_back({FlatYInput, 7, 1, 1, 1, 32});
  items.push_back({VegetationDensityLabel, 8, 0, 1, 1, 28});
  items.push_back({VegetationDensityInput, 8, 1, 1, 1, 32});
  items.push_back({DecorationDensityLabel, 9, 0, 1, 1, 28});
  items.push_back({DecorationDensityInput, 9, 1, 1, 1, 32});
  items.push_back({StructureDensityLabel, 10, 0, 1, 1, 28});
  items.push_back({StructureDensityInput, 10, 1, 1, 1, 32});
  items.push_back({TerrainRoughnessLabel, 11, 0, 1, 1, 28});
  items.push_back({TerrainRoughnessInput, 11, 1, 1, 1, 32});
  items.push_back({BiomeForestLabel, 12, 0, 1, 1, 28});
  items.push_back({BiomeForestInput, 12, 1, 1, 1, 32});
  items.push_back({BiomeDesertLabel, 13, 0, 1, 1, 28});
  items.push_back({BiomeDesertInput, 13, 1, 1, 1, 32});
  items.push_back({BiomePlainsLabel, 14, 0, 1, 1, 28});
  items.push_back({BiomePlainsInput, 14, 1, 1, 1, 32});
  items.push_back({BiomeHillsLabel, 15, 0, 1, 1, 28});
  items.push_back({BiomeHillsInput, 15, 1, 1, 1, 32});
  items.push_back({BiomeTundraLabel, 16, 0, 1, 1, 28});
  items.push_back({BiomeTundraInput, 16, 1, 1, 1, 32});
  items.push_back({BiomeBlendLabel, 17, 0, 1, 1, 28});
  items.push_back({BiomeBlendInput, 17, 1, 1, 1, 32});
  items.push_back({TerrainErosionLabel, 18, 0, 1, 1, 28});
  items.push_back({TerrainErosionInput, 18, 1, 1, 1, 32});
  items.push_back({OreDensityLabel, 19, 0, 1, 1, 28});
  items.push_back({OreDensityInput, 19, 1, 1, 1, 32});
  items.push_back({CaveThresholdLabel, 20, 0, 1, 1, 28});
  items.push_back({CaveThresholdInput, 20, 1, 1, 1, 32});
  items.push_back({CaveMinYLabel, 21, 0, 1, 1, 28});
  items.push_back({CaveMinYInput, 21, 1, 1, 1, 32});
  items.push_back({CaveScaleLabel, 22, 0, 1, 1, 28});
  items.push_back({CaveScaleInput, 22, 1, 1, 1, 32});
  items.push_back({CaveMaxDepthLabel, 23, 0, 1, 1, 28});
  items.push_back({CaveMaxDepthInput, 23, 1, 1, 1, 32});
  items.push_back({CaveStyleLabel, 24, 0, 1, 1, 28});
  items.push_back({CaveStyleInput, 24, 1, 1, 1, 32});
  items.push_back({CavesBox, 25, 0, 1, 1, 30});
  items.push_back({OresBox, 25, 1, 1, 1, 30});
  items.push_back({TreesBox, 26, 0, 1, 1, 30});
  items.push_back({DecorationBox, 26, 1, 1, 1, 30});
  items.push_back({StructuresBox, 27, 0, 1, 1, 30});
  items.push_back({WaterBox, 27, 1, 1, 1, 30});
  items.push_back({LavaBox, 28, 0, 1, 1, 30});
  items.push_back({FireBox, 28, 1, 1, 1, 30});
  items.push_back({WorldGenPackIdLabel, 29, 0, 1, 1, 28});
  items.push_back({WorldGenPackIdInput, 29, 1, 1, 1, 32});
  return items;
}

} // namespace cutum
