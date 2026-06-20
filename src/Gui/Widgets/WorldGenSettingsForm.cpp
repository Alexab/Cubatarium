#include "Gui/Widgets/WorldGenSettingsForm.h"
#include "Gui/Core/GuiTheme.h"
#include "Gui/Widgets/GuiButton.h"
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

std::string VerticalLabel(VerticalMode m) { return VerticalModeToString(m); }

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
  if (VerticalBtn)
  {
    VerticalBtn->SetLabel(VerticalLabel(FormSettings.Vertical));
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
  if (CavesBox)
  {
    CavesBox->SetChecked(FormSettings.EnableCaves);
  }
  if (TreesBox)
  {
    TreesBox->SetChecked(FormSettings.EnableTrees);
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
}

ProceduralSettings UWorldGenSettingsForm::ReadSettings() const
{
  ProceduralSettings s = FormSettings;
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
  if (CavesBox)
  {
    s.EnableCaves = CavesBox->IsChecked();
  }
  if (TreesBox)
  {
    s.EnableTrees = TreesBox->IsChecked();
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
  const uint32_t flags =
      descriptor ? descriptor->FeatureFlags : kFeatureVertical;

  const bool showFlat = (flags & kFeatureFlatSurfaceY) != 0;
  const bool showCaves = (flags & kFeatureCaves) != 0;
  const bool showTrees = (flags & kFeatureTrees) != 0;
  const bool showFluids = (flags & kFeatureFluids) != 0;
  const bool showTuning = (flags & kFeatureTuning) != 0;
  const bool showBiomeTuning = (flags & kFeatureBiomes) != 0;
  const bool showStructures = (flags & kFeatureStructures) != 0;

  SetWidgetVisible(FlatYLabel, showFlat);
  SetWidgetVisible(FlatYInput, showFlat);
  SetWidgetVisible(CavesBox, showCaves);
  SetWidgetVisible(TreesBox, showTrees);
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
}

void UWorldGenSettingsForm::CycleVertical()
{
  FormSettings.Vertical = FormSettings.Vertical == VerticalMode::Compact
                              ? VerticalMode::Extended
                              : VerticalMode::Compact;
  ApplyVerticalModeDefaults(FormSettings);
  SetSettings(FormSettings);
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
  genList->SetSelectedIndex(
      UWorldGeneratorRegistry::IndexOf(FormSettings.Generator));
  genList->SetOnSelectionChanged([this](int index) { OnGeneratorSelected(index); });
  panel.AddChild(std::move(genList));

  auto genDesc = std::make_unique<UGuiLabel>(Theme, "");
  GeneratorDescLabel = genDesc.get();
  panel.AddChild(std::move(genDesc));

  auto vertLabel = std::make_unique<UGuiLabel>(Theme, "Vertical:");
  VerticalCaption = vertLabel.get();
  panel.AddChild(std::move(vertLabel));
  auto vertBtn =
      std::make_unique<UGuiButton>(Theme, VerticalLabel(FormSettings.Vertical));
  VerticalBtn = vertBtn.get();
  vertBtn->SetOnClick([this]() { CycleVertical(); });
  panel.AddChild(std::move(vertBtn));

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

  auto forestLabel = std::make_unique<UGuiLabel>(Theme, "Forest biome weight:");
  BiomeForestLabel = forestLabel.get();
  panel.AddChild(std::move(forestLabel));
  auto forestIn = std::make_unique<UGuiTextInput>(Theme);
  BiomeForestInput = forestIn.get();
  forestIn->SetText(std::to_string(FormSettings.Tuning.biomeForestWeight));
  panel.AddChild(std::move(forestIn));

  auto desertLabel = std::make_unique<UGuiLabel>(Theme, "Desert biome weight:");
  BiomeDesertLabel = desertLabel.get();
  panel.AddChild(std::move(desertLabel));
  auto desertIn = std::make_unique<UGuiTextInput>(Theme);
  BiomeDesertInput = desertIn.get();
  desertIn->SetText(std::to_string(FormSettings.Tuning.biomeDesertWeight));
  panel.AddChild(std::move(desertIn));

  auto caves = std::make_unique<UGuiCheckbox>(Theme, "Caves");
  CavesBox = caves.get();
  caves->SetChecked(FormSettings.EnableCaves);
  caves->SetOnChanged([this](bool v) { FormSettings.EnableCaves = v; });
  panel.AddChild(std::move(caves));

  auto trees = std::make_unique<UGuiCheckbox>(Theme, "Trees");
  TreesBox = trees.get();
  trees->SetChecked(FormSettings.EnableTrees);
  trees->SetOnChanged([this](bool v) { FormSettings.EnableTrees = v; });
  panel.AddChild(std::move(trees));

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
}

std::vector<GuiGridItem> UWorldGenSettingsForm::BuildGridItems() const
{
  std::vector<GuiGridItem> items;
  items.push_back({HintLabel, 0, 0, 1, 2, 28});
  items.push_back({GeneratorCaption, 1, 0, 1, 1, 28});
  items.push_back({GeneratorList, 1, 1, 1, 1, 150});
  items.push_back({GeneratorDescLabel, 2, 0, 1, 2, 36});
  items.push_back({VerticalCaption, 3, 0, 1, 1, 28});
  items.push_back({VerticalBtn, 3, 1, 1, 1, 32});
  items.push_back({SeedLabel, 4, 0, 1, 1, 28});
  items.push_back({SeedInput, 4, 1, 1, 1, 32});
  items.push_back({SeaLevelLabel, 5, 0, 1, 1, 28});
  items.push_back({SeaLevelInput, 5, 1, 1, 1, 32});
  items.push_back({MaxHeightLabel, 6, 0, 1, 1, 28});
  items.push_back({MaxHeightInput, 6, 1, 1, 1, 32});
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
  items.push_back({CavesBox, 14, 0, 1, 1, 30});
  items.push_back({TreesBox, 14, 1, 1, 1, 30});
  items.push_back({WaterBox, 15, 0, 1, 1, 30});
  items.push_back({LavaBox, 15, 1, 1, 1, 30});
  items.push_back({FireBox, 16, 0, 1, 2, 30});
  return items;
}

} // namespace cutum
