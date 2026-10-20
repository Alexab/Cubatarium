#include "Gui/Widgets/WorldGenSettingsForm.h"
#include "Gui/Core/GuiTheme.h"
#include "Gui/Widgets/GuiCheckbox.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/GuiListView.h"
#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Widgets/GuiSliderField.h"
#include "Gui/Widgets/GuiTabBar.h"
#include "Gui/Widgets/GuiTextInput.h"
#include "Gui/Widgets/GuiWidget.h"
#include "WorldGen/Core/WorldGeneratorDescriptor.h"
#include "WorldGen/Core/WorldSeedParser.h"
#include <algorithm>
#include <sstream>

namespace cutum
{

namespace
{
int PresetIndexFromId(const std::string &id)
{
  if (id == "realistic")
  {
    return 1;
  }
  if (id == "sparse_structures")
  {
    return 2;
  }
  if (id == "smooth")
  {
    return 3;
  }
  return 0;
}

std::string PresetIdFromIndex(int index)
{
  switch (index)
  {
  case 1:
    return "realistic";
  case 2:
    return "sparse_structures";
  case 3:
    return "smooth";
  default:
    return "balanced";
  }
}

void SetWidgetVisible(UGuiWidget *widget, bool visible)
{
  if (widget)
  {
    widget->SetVisible(visible);
  }
}

bool IsCompactGenerator(ProceduralGenerator generator)
{
  return generator == ProceduralGenerator::Flat ||
         generator == ProceduralGenerator::Heightmap;
}

/// Defaults for the generator + preset currently shown in the form; used as the
/// target of every "Reset" button.
ProceduralSettings DefaultsFor(const ProceduralSettings &current)
{
  ProceduralSettings defaults;
  defaults.Generator = current.Generator;
  ResetToGeneratorDefaults(defaults);
  defaults.WorldGenProfileVersion = current.WorldGenProfileVersion;
  defaults.TerrainBackendMode = current.TerrainBackendMode;
  ApplyWorldGenPreset(defaults, current.WorldGenPresetId);
  ResolveProceduralDefaults(defaults);
  return defaults;
}

} // namespace

UWorldGenSettingsForm::UWorldGenSettingsForm(const GuiTheme *theme)
    : Theme(theme)
{
}

void UWorldGenSettingsForm::WireTerrainQualityHint(UGuiCheckbox &box,
                                                   const char *description)
{
  box.SetDescription(description);
  box.SetOnDescriptionHover(
      [this](const std::string &text)
      {
        if (TerrainQualityHintLabel)
        {
          TerrainQualityHintLabel->SetText(text);
        }
      });
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
  FormSettings.Generator = ProceduralGenerator::Overworld;
  ResetToGeneratorDefaults(FormSettings);
  FormSettings.TerrainBackendMode = TerrainBackend::Density3D;
  FormSettings.WorldGenProfileVersion = 3;
  // "Balanced" is the preset the worldgen quality gates are calibrated on.
  ApplyWorldGenPreset(FormSettings, "balanced");
  // A fresh world should not reuse the seed saved in config.json: leave the
  // box empty so every new world gets its own random seed.
  FormSettings.SeedText.clear();
  SeedBlank = true;
  SetHintText("Choose generator and tuning for this new world.");
}

void UWorldGenSettingsForm::SetOnLayoutChanged(std::function<void()> handler)
{
  OnLayoutChanged = std::move(handler);
}

void UWorldGenSettingsForm::ConfigureRangesAndDefaults()
{
  const ProceduralSettings d = DefaultsFor(FormSettings);
  const bool compact = IsCompactGenerator(FormSettings.Generator);
  const auto setup = [](UGuiSliderField *field, float min, float max,
                        float step, int decimals, float def)
  {
    if (field)
    {
      field->Configure(min, max, step, decimals);
      field->SetDefault(def);
    }
  };
  setup(SeaLevelField, compact ? 2.f : 16.f, compact ? 13.f : 160.f, 1.f, 0,
        static_cast<float>(d.SeaLevel));
  setup(MaxHeightField, compact ? 8.f : 96.f, compact ? 15.f : 319.f, 1.f, 0,
        static_cast<float>(d.MaxHeight));
  setup(BedrockTopYField, static_cast<float>(d.MinHeight),
        static_cast<float>(d.MinHeight + (compact ? 4 : 16)), 1.f, 0,
        static_cast<float>(d.BedrockTopY));
  setup(FlatYField, 1.f, static_cast<float>(std::max(2, d.MaxHeight)), 1.f, 0,
        static_cast<float>(d.FlatSurfaceY));
  setup(VegetationDensityField, 0.f, 2.f, 0.05f, 2,
        d.Tuning.vegetationDensity);
  setup(DecorationDensityField, 0.f, 2.f, 0.05f, 2,
        d.Tuning.decorationDensity);
  setup(StructureDensityField, 0.f, 2.f, 0.05f, 2, d.Tuning.structureDensity);
  setup(TerrainRoughnessField, 0.25f, 2.f, 0.05f, 2, d.Tuning.terrainRoughness);
  setup(BiomeForestField, 0.f, 2.f, 0.05f, 2, d.Tuning.biomeForestWeight);
  setup(BiomeDesertField, 0.f, 2.f, 0.05f, 2, d.Tuning.biomeDesertWeight);
  setup(BiomePlainsField, 0.f, 2.f, 0.05f, 2, d.Tuning.biomePlainsWeight);
  setup(BiomeHillsField, 0.f, 2.f, 0.05f, 2, d.Tuning.biomeHillsWeight);
  setup(BiomeTundraField, 0.f, 2.f, 0.05f, 2, d.Tuning.biomeTundraWeight);
  setup(BiomeBlendField, 0.f, 16.f, 1.f, 0, d.Tuning.biomeBlendRadius);
  setup(OreDensityField, 0.f, 2.f, 0.05f, 2, d.Tuning.oreDensity);
  setup(TerrainErosionField, 0.f, 1.f, 0.01f, 2, d.Tuning.terrainErosion);
  setup(CaveThresholdField, 0.f, 1.f, 0.01f, 2, d.Caves.threshold);
  setup(CaveMinYField, static_cast<float>(d.MinHeight), 120.f, 1.f, 0,
        static_cast<float>(d.Caves.minY));
  setup(CaveScaleField, 0.01f, 0.2f, 0.005f, 3, d.Caves.scale);
  setup(CaveMaxDepthField, 0.f, 96.f, 1.f, 0,
        static_cast<float>(d.Caves.maxDepthBelowSurface));
  setup(RavineRarityField, 100.f, 2000.f, 50.f, 0,
        static_cast<float>(d.Ravines.rarity));
  setup(RavineAquaticMaxDepthField, 0.f, 16.f, 1.f, 0,
        static_cast<float>(d.Ravines.aquaticMaxDepth));
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
  if (PresetList)
  {
    PresetList->SetSelectedIndex(PresetIndexFromId(FormSettings.WorldGenPresetId));
  }
  if (WorldGenPackList)
  {
    int selected = 0;
    for (size_t i = 0; i < PackInfos.size(); ++i)
    {
      if (PackInfos[i].Id == FormSettings.WorldGenPackId)
      {
        selected = static_cast<int>(i);
        break;
      }
    }
    WorldGenPackList->SetSelectedIndex(selected);
    RefreshWorldGenPackDescription();
  }
  if (SeedInput)
  {
    if (SeedBlank && FormSettings.SeedText.empty())
    {
      SeedInput->SetText("");
    }
    else
    {
      SeedInput->SetText(FormSettings.SeedText.empty()
                             ? std::to_string(FormSettings.Seed)
                             : FormSettings.SeedText);
    }
  }
  if (TerrainBackendList)
  {
    TerrainBackendList->SetSelectedIndex(
        FormSettings.TerrainBackendMode == TerrainBackend::Density3D ? 1 : 0);
  }
  if (CaveStyleList)
  {
    CaveStyleList->SetSelectedIndex(
        FormSettings.Caves.style == CaveStyle::Worm ? 1 : 0);
  }

  ConfigureRangesAndDefaults();
  const auto put = [](UGuiSliderField *field, float value)
  {
    if (field)
    {
      field->SetValue(value);
    }
  };
  put(SeaLevelField, static_cast<float>(FormSettings.SeaLevel));
  put(MaxHeightField, static_cast<float>(FormSettings.MaxHeight));
  put(BedrockTopYField, static_cast<float>(FormSettings.BedrockTopY));
  put(FlatYField, static_cast<float>(FormSettings.FlatSurfaceY));
  put(VegetationDensityField, FormSettings.Tuning.vegetationDensity);
  put(DecorationDensityField, FormSettings.Tuning.decorationDensity);
  put(StructureDensityField, FormSettings.Tuning.structureDensity);
  put(TerrainRoughnessField, FormSettings.Tuning.terrainRoughness);
  put(BiomeForestField, FormSettings.Tuning.biomeForestWeight);
  put(BiomeDesertField, FormSettings.Tuning.biomeDesertWeight);
  put(BiomePlainsField, FormSettings.Tuning.biomePlainsWeight);
  put(BiomeHillsField, FormSettings.Tuning.biomeHillsWeight);
  put(BiomeTundraField, FormSettings.Tuning.biomeTundraWeight);
  put(BiomeBlendField, FormSettings.Tuning.biomeBlendRadius);
  put(OreDensityField, FormSettings.Tuning.oreDensity);
  put(TerrainErosionField, FormSettings.Tuning.terrainErosion);
  put(CaveThresholdField, FormSettings.Caves.threshold);
  put(CaveMinYField, static_cast<float>(FormSettings.Caves.minY));
  put(CaveScaleField, FormSettings.Caves.scale);
  put(CaveMaxDepthField,
      static_cast<float>(FormSettings.Caves.maxDepthBelowSurface));
  put(RavineRarityField, static_cast<float>(FormSettings.Ravines.rarity));
  put(RavineAquaticMaxDepthField,
      static_cast<float>(FormSettings.Ravines.aquaticMaxDepth));

  const auto check = [](UGuiCheckbox *box, bool value)
  {
    if (box)
    {
      box->SetChecked(value);
    }
  };
  check(CavesBox, FormSettings.EnableCaves);
  check(RavinesBox, FormSettings.Ravines.enabled);
  check(RavineFillWaterBox, FormSettings.Ravines.fillWater);
  check(OresBox, FormSettings.EnableOres);
  check(TreesBox, FormSettings.EnableTrees);
  check(GroundCoverBox, FormSettings.EnableGroundCover);
  check(DecorationBox, FormSettings.EnableDecoration);
  check(StructuresBox, FormSettings.EnableStructures);
  check(WaterBox, FormSettings.FillWater);
  check(LavaBox, FormSettings.FillLava);
  check(FireBox, FormSettings.FillFire);
  check(UnifiedHeightFieldBox, FormSettings.Tuning.useUnifiedHeightField);
  check(AnalyticValleysBox, FormSettings.Tuning.useAnalyticValleys);
  check(MudflowErosionBox, FormSettings.Tuning.useMudflowErosion);
  check(DensityRefineParityBox, FormSettings.Tuning.useDensityRefineParity);

  RefreshGeneratorDescription();
  UpdateFieldVisibility();
  if (OnLayoutChanged)
  {
    OnLayoutChanged();
  }
}

ProceduralSettings UWorldGenSettingsForm::ReadSettings() const
{
  // The preset is applied when it is selected (OnPresetSelected), never here:
  // re-applying it on read would silently overwrite every value typed or
  // dragged after the selection.
  ProceduralSettings s = FormSettings;
  if (WorldGenPackList && !PackInfos.empty())
  {
    const int selected = WorldGenPackList->GetSelectedIndex();
    if (selected >= 0 && selected < static_cast<int>(PackInfos.size()))
    {
      s.WorldGenPackId = PackInfos[static_cast<size_t>(selected)].Id;
    }
  }
  if (PresetList)
  {
    s.WorldGenPresetId = PresetIdFromIndex(PresetList->GetSelectedIndex());
  }
  if (SeedInput)
  {
    const WorldSeedResolution resolved = ResolveWorldSeed(SeedInput->GetText());
    s.Seed = resolved.resolved;
    s.SeedText = resolved.raw;
    s.SeedKind = resolved.kind;
  }
  const auto geti = [](const UGuiSliderField *field, int fallback)
  { return field ? field->GetIntValue() : fallback; };
  const auto getf = [](const UGuiSliderField *field, float fallback)
  { return field ? field->GetValue() : fallback; };
  s.SeaLevel = geti(SeaLevelField, s.SeaLevel);
  s.MaxHeight = geti(MaxHeightField, s.MaxHeight);
  s.BedrockTopY = geti(BedrockTopYField, s.BedrockTopY);
  s.FlatSurfaceY = geti(FlatYField, s.FlatSurfaceY);
  s.Tuning.vegetationDensity =
      getf(VegetationDensityField, s.Tuning.vegetationDensity);
  s.Tuning.decorationDensity =
      getf(DecorationDensityField, s.Tuning.decorationDensity);
  s.Tuning.structureDensity =
      getf(StructureDensityField, s.Tuning.structureDensity);
  s.Tuning.terrainRoughness =
      getf(TerrainRoughnessField, s.Tuning.terrainRoughness);
  s.Tuning.biomeForestWeight = getf(BiomeForestField, s.Tuning.biomeForestWeight);
  s.Tuning.biomeDesertWeight = getf(BiomeDesertField, s.Tuning.biomeDesertWeight);
  s.Tuning.biomePlainsWeight = getf(BiomePlainsField, s.Tuning.biomePlainsWeight);
  s.Tuning.biomeHillsWeight = getf(BiomeHillsField, s.Tuning.biomeHillsWeight);
  s.Tuning.biomeTundraWeight = getf(BiomeTundraField, s.Tuning.biomeTundraWeight);
  s.Tuning.biomeBlendRadius = getf(BiomeBlendField, s.Tuning.biomeBlendRadius);
  s.Tuning.oreDensity = getf(OreDensityField, s.Tuning.oreDensity);
  s.Tuning.terrainErosion = getf(TerrainErosionField, s.Tuning.terrainErosion);
  s.Caves.threshold = getf(CaveThresholdField, s.Caves.threshold);
  s.Caves.minY = geti(CaveMinYField, s.Caves.minY);
  s.Caves.scale = getf(CaveScaleField, s.Caves.scale);
  s.Caves.maxDepthBelowSurface =
      geti(CaveMaxDepthField, s.Caves.maxDepthBelowSurface);
  s.Ravines.rarity = std::max(1, geti(RavineRarityField, s.Ravines.rarity));
  s.Ravines.aquaticMaxDepth =
      std::max(0, geti(RavineAquaticMaxDepthField, s.Ravines.aquaticMaxDepth));
  if (TerrainBackendList)
  {
    s.TerrainBackendMode = TerrainBackendList->GetSelectedIndex() == 1
                               ? TerrainBackend::Density3D
                               : TerrainBackend::Heightmap;
  }
  if (CaveStyleList)
  {
    s.Caves.style = CaveStyleList->GetSelectedIndex() == 1 ? CaveStyle::Worm
                                                           : CaveStyle::Noise;
  }
  if (CavesBox)
  {
    s.EnableCaves = CavesBox->IsChecked();
  }
  if (RavinesBox)
  {
    s.Ravines.enabled = RavinesBox->IsChecked();
  }
  if (RavineFillWaterBox)
  {
    s.Ravines.fillWater = RavineFillWaterBox->IsChecked();
  }
  if (OresBox)
  {
    s.EnableOres = OresBox->IsChecked();
  }
  if (TreesBox)
  {
    s.EnableTrees = TreesBox->IsChecked();
  }
  if (GroundCoverBox)
  {
    s.EnableGroundCover = GroundCoverBox->IsChecked();
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
  if (UnifiedHeightFieldBox)
  {
    s.Tuning.useUnifiedHeightField = UnifiedHeightFieldBox->IsChecked();
  }
  if (AnalyticValleysBox)
  {
    s.Tuning.useAnalyticValleys = AnalyticValleysBox->IsChecked();
  }
  if (MudflowErosionBox)
  {
    s.Tuning.useMudflowErosion = MudflowErosionBox->IsChecked();
  }
  if (DensityRefineParityBox)
  {
    s.Tuning.useDensityRefineParity = DensityRefineParityBox->IsChecked();
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
  const std::string seedText = SeedInput ? SeedInput->GetText() : std::string();
  const std::string presetId = FormSettings.WorldGenPresetId;
  FormSettings.Generator =
      UWorldGeneratorRegistry::Get(static_cast<size_t>(index)).Id;
  ResetToGeneratorDefaults(FormSettings);
  FormSettings.Seed = seed;
  FormSettings.WorldGenProfileVersion = std::max(
      FormSettings.WorldGenProfileVersion, ForNewWorldScreen ? 3 : 0);
  if (ForNewWorldScreen)
  {
    FormSettings.TerrainBackendMode = TerrainBackend::Density3D;
  }
  // Keep the selected preset across generator changes.
  ApplyWorldGenPreset(FormSettings, presetId);
  SetSettings(FormSettings);
  if (SeedInput)
  {
    SeedInput->SetText(seedText);
  }
}

void UWorldGenSettingsForm::OnPresetSelected(int index)
{
  const std::string seedText = SeedInput ? SeedInput->GetText() : std::string();
  ProceduralSettings current = ReadSettings();
  ApplyWorldGenPreset(current, PresetIdFromIndex(index));
  SetSettings(current);
  if (SeedInput)
  {
    SeedInput->SetText(seedText);
  }
}

void UWorldGenSettingsForm::OnTerrainBackendSelected(int index)
{
  FormSettings.TerrainBackendMode =
      index == 1 ? TerrainBackend::Density3D : TerrainBackend::Heightmap;
  UpdateFieldVisibility();
  if (OnLayoutChanged)
  {
    OnLayoutChanged();
  }
}

void UWorldGenSettingsForm::OnTabChanged(int tab)
{
  ActiveTab = tab;
  UpdateFieldVisibility();
  if (OnLayoutChanged)
  {
    OnLayoutChanged();
  }
}

void UWorldGenSettingsForm::OnWorldGenPackSelected(int index)
{
  if (index < 0 || index >= static_cast<int>(PackInfos.size()))
  {
    return;
  }
  FormSettings.WorldGenPackId = PackInfos[static_cast<size_t>(index)].Id;
  RefreshWorldGenPackDescription();
}

void UWorldGenSettingsForm::RefreshWorldGenPackDescription()
{
  if (!WorldGenPackDescLabel || !WorldGenPackList)
  {
    return;
  }
  const int index = WorldGenPackList->GetSelectedIndex();
  if (index < 0 || index >= static_cast<int>(PackInfos.size()))
  {
    WorldGenPackDescLabel->SetText("");
    return;
  }
  const WorldGenPackInfo &info = PackInfos[static_cast<size_t>(index)];
  if (!info.Description.empty())
  {
    WorldGenPackDescLabel->SetText(info.Description);
  }
  else
  {
    WorldGenPackDescLabel->SetText("Pack: " + info.Id);
  }
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

  const bool basic = ActiveTab == 0;
  const bool advanced = !basic;
  const auto vis = [](UGuiWidget *w, bool flag, bool inTab)
  { SetWidgetVisible(w, flag && inTab); };

  // The explicit carver parameters are dead when caves come from the density
  // field (every Overworld pack); only show them for generators that use them.
  const bool legacyCaves = showCaves && !FormSettings.Caves.useDensityField;
  // "Unified height field" and "Density refine parity" only act on the
  // heightmap pipeline / worldgen profiles before 3.
  const bool legacyTerrainFlags =
      FormSettings.TerrainBackendMode == TerrainBackend::Heightmap ||
      FormSettings.WorldGenProfileVersion < 3;

  // Basic tab.
  vis(GeneratorCaption, true, basic);
  vis(GeneratorList, true, basic);
  vis(GeneratorDescLabel, true, basic);
  vis(PresetLabel, true, basic);
  vis(PresetList, true, basic);
  vis(SeedLabel, true, basic);
  vis(SeedInput, true, basic);
  vis(FlatYLabel, showFlat, basic);
  vis(FlatYField, showFlat, basic);
  vis(TerrainRoughnessLabel, showTuning, basic);
  vis(TerrainRoughnessField, showTuning, basic);
  vis(VegetationDensityLabel, showTrees, basic);
  vis(VegetationDensityField, showTrees, basic);
  vis(StructureDensityLabel, showStructures, basic);
  vis(StructureDensityField, showStructures, basic);
  vis(TreesBox, showTrees, basic);
  vis(StructuresBox, showStructures, basic);
  vis(CavesBox, showCaves, basic);
  vis(OresBox, showOres, basic);
  vis(WaterBox, showFluids, basic);

  // Advanced tab.
  vis(SeaLevelLabel, true, advanced);
  vis(SeaLevelField, true, advanced);
  vis(MaxHeightLabel, true, advanced);
  vis(MaxHeightField, true, advanced);
  vis(BedrockTopYLabel, showTuning, advanced);
  vis(BedrockTopYField, showTuning, advanced);
  vis(TerrainBackendLabel, showTuning, advanced);
  vis(TerrainBackendList, showTuning, advanced);
  vis(WorldGenPackIdLabel, showBiomeTuning, advanced);
  vis(WorldGenPackList, showBiomeTuning, advanced);
  vis(WorldGenPackDescLabel, showBiomeTuning, advanced);
  vis(DecorationDensityLabel, showTrees, advanced);
  vis(DecorationDensityField, showTrees, advanced);
  vis(GroundCoverBox, showTrees, advanced);
  vis(DecorationBox, showTrees, advanced);
  vis(OreDensityLabel, showOres, advanced);
  vis(OreDensityField, showOres, advanced);
  vis(TerrainErosionLabel, showBiomeTuning, advanced);
  vis(TerrainErosionField, showBiomeTuning, advanced);
  vis(BiomeBlendLabel, showBiomeTuning, advanced);
  vis(BiomeBlendField, showBiomeTuning, advanced);
  vis(BiomeForestLabel, showBiomeTuning, advanced);
  vis(BiomeForestField, showBiomeTuning, advanced);
  vis(BiomeDesertLabel, showBiomeTuning, advanced);
  vis(BiomeDesertField, showBiomeTuning, advanced);
  vis(BiomePlainsLabel, showBiomeTuning, advanced);
  vis(BiomePlainsField, showBiomeTuning, advanced);
  vis(BiomeHillsLabel, showBiomeTuning, advanced);
  vis(BiomeHillsField, showBiomeTuning, advanced);
  vis(BiomeTundraLabel, showBiomeTuning, advanced);
  vis(BiomeTundraField, showBiomeTuning, advanced);
  vis(CaveMinYLabel, showCaves, advanced);
  vis(CaveMinYField, showCaves, advanced);
  vis(CaveMaxDepthLabel, showCaves, advanced);
  vis(CaveMaxDepthField, showCaves, advanced);
  vis(CaveThresholdLabel, legacyCaves, advanced);
  vis(CaveThresholdField, legacyCaves, advanced);
  vis(CaveScaleLabel, legacyCaves, advanced);
  vis(CaveScaleField, legacyCaves, advanced);
  vis(CaveStyleLabel, legacyCaves, advanced);
  vis(CaveStyleList, legacyCaves, advanced);
  vis(RavinesBox, showCaves, advanced);
  vis(RavineRarityLabel, showCaves, advanced);
  vis(RavineRarityField, showCaves, advanced);
  vis(RavineAquaticMaxDepthLabel, showCaves, advanced);
  vis(RavineAquaticMaxDepthField, showCaves, advanced);
  vis(RavineFillWaterBox, showCaves, advanced);
  vis(LavaBox, showFluids, advanced);
  vis(FireBox, showFluids, advanced);
  vis(TerrainQualityLabel, true, advanced);
  vis(UnifiedHeightFieldBox, legacyTerrainFlags, advanced);
  vis(DensityRefineParityBox, legacyTerrainFlags, advanced);
  vis(AnalyticValleysBox, true, advanced);
  vis(MudflowErosionBox, true, advanced);
  vis(TerrainQualityHintLabel, true, advanced);
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

UGuiLabel *UWorldGenSettingsForm::AddLabel(UGuiPanel &panel,
                                           const std::string &text)
{
  auto label = std::make_unique<UGuiLabel>(Theme, text);
  UGuiLabel *raw = label.get();
  panel.AddChild(std::move(label));
  return raw;
}

UGuiSliderField *UWorldGenSettingsForm::AddSlider(UGuiPanel &panel,
                                                  const std::string &label,
                                                  UGuiLabel *&labelOut,
                                                  float min, float max,
                                                  float step, int decimals)
{
  labelOut = AddLabel(panel, label);
  auto field = std::make_unique<UGuiSliderField>(Theme);
  UGuiSliderField *raw = field.get();
  raw->Configure(min, max, step, decimals);
  panel.AddChild(std::move(field));
  return raw;
}

UGuiCheckbox *UWorldGenSettingsForm::AddCheckbox(
    UGuiPanel &panel, const std::string &label,
    std::function<void(bool)> onChanged)
{
  auto box = std::make_unique<UGuiCheckbox>(Theme, label);
  UGuiCheckbox *raw = box.get();
  if (onChanged)
  {
    raw->SetOnChanged(std::move(onChanged));
  }
  panel.AddChild(std::move(box));
  return raw;
}

void UWorldGenSettingsForm::AddWidgetsTo(UGuiPanel &panel)
{
  HintLabel = AddLabel(panel, "Defaults for the next new worlds.");

  auto tabs = std::make_unique<UGuiTabBar>(Theme);
  TabBar = tabs.get();
  tabs->SetTabs({"Basic", "Advanced"});
  tabs->SetActiveTab(ActiveTab);
  tabs->SetOnTabChanged([this](int tab) { OnTabChanged(tab); });
  panel.AddChild(std::move(tabs));

  // ---- Basic tab -------------------------------------------------------
  GeneratorCaption = AddLabel(panel, "Generator:");
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

  GeneratorDescLabel = AddLabel(panel, "");

  PresetLabel = AddLabel(panel, "Preset:");
  auto presetList = std::make_unique<UGuiListView>(Theme);
  PresetList = presetList.get();
  presetList->SetItems({"Balanced", "Realistic", "Sparse structures", "Smooth terrain"});
  presetList->SetVisibleRowCount(4);
  presetList->SetSelectedIndex(PresetIndexFromId(FormSettings.WorldGenPresetId));
  presetList->SetOnSelectionChanged([this](int index) { OnPresetSelected(index); });
  panel.AddChild(std::move(presetList));

  SeedLabel = AddLabel(panel, "World seed (empty = random):");
  auto seedIn = std::make_unique<UGuiTextInput>(Theme);
  SeedInput = seedIn.get();
  seedIn->SetText(std::to_string(FormSettings.Seed));
  panel.AddChild(std::move(seedIn));

  FlatYField = AddSlider(panel, "Flat surface Y:", FlatYLabel, 1.f, 15.f, 1.f, 0);
  TerrainRoughnessField =
      AddSlider(panel, "Terrain roughness:", TerrainRoughnessLabel, 0.25f, 2.f,
                0.05f, 2);
  VegetationDensityField = AddSlider(panel, "Vegetation density:",
                                     VegetationDensityLabel, 0.f, 2.f, 0.05f, 2);
  StructureDensityField = AddSlider(panel, "Structure density:",
                                    StructureDensityLabel, 0.f, 2.f, 0.05f, 2);
  TreesBox = AddCheckbox(panel, "Trees",
                         [this](bool v) { FormSettings.EnableTrees = v; });
  StructuresBox = AddCheckbox(panel, "Structures",
                              [this](bool v) { FormSettings.EnableStructures = v; });
  CavesBox = AddCheckbox(panel, "Caves",
                         [this](bool v) { FormSettings.EnableCaves = v; });
  OresBox = AddCheckbox(panel, "Ores",
                        [this](bool v) { FormSettings.EnableOres = v; });
  WaterBox = AddCheckbox(panel, "Fill water (oceans, lakes)",
                         [this](bool v) { FormSettings.FillWater = v; });

  // ---- Advanced tab ----------------------------------------------------
  SeaLevelField = AddSlider(panel, "Sea level:", SeaLevelLabel, 16.f, 160.f, 1.f, 0);
  MaxHeightField =
      AddSlider(panel, "Max height:", MaxHeightLabel, 96.f, 319.f, 1.f, 0);
  BedrockTopYField = AddSlider(panel, "Bedrock top Y:", BedrockTopYLabel, -64.f,
                               -48.f, 1.f, 0);

  auto backendLabel = AddLabel(panel, "Terrain backend:");
  TerrainBackendLabel = backendLabel;
  auto backendList = std::make_unique<UGuiListView>(Theme);
  TerrainBackendList = backendList.get();
  backendList->SetItems({"Heightmap (legacy)", "3D density"});
  backendList->SetVisibleRowCount(2);
  backendList->SetSelectedIndex(
      FormSettings.TerrainBackendMode == TerrainBackend::Density3D ? 1 : 0);
  backendList->SetOnSelectionChanged(
      [this](int index) { OnTerrainBackendSelected(index); });
  panel.AddChild(std::move(backendList));

  auto packLabel = AddLabel(panel, "Worldgen pack:");
  WorldGenPackIdLabel = packLabel;
  PackInfos = UWorldGenPack::ListPackInfos();
  std::vector<std::string> pack_items;
  pack_items.reserve(PackInfos.size());
  for (const WorldGenPackInfo &info : PackInfos)
  {
    pack_items.push_back(info.Id);
  }
  if (pack_items.empty())
  {
    pack_items.push_back(FormSettings.WorldGenPackId.empty() ? "default"
                                                             : FormSettings.WorldGenPackId);
    PackInfos.push_back({pack_items.front(), ""});
  }
  auto packList = std::make_unique<UGuiListView>(Theme);
  WorldGenPackList = packList.get();
  packList->SetItems(std::move(pack_items));
  packList->SetVisibleRowCount(3);
  int pack_selected = 0;
  for (size_t i = 0; i < PackInfos.size(); ++i)
  {
    if (PackInfos[i].Id == FormSettings.WorldGenPackId)
    {
      pack_selected = static_cast<int>(i);
      break;
    }
  }
  packList->SetSelectedIndex(pack_selected);
  packList->SetOnSelectionChanged(
      [this](int index) { OnWorldGenPackSelected(index); });
  panel.AddChild(std::move(packList));
  WorldGenPackDescLabel = AddLabel(panel, "");
  RefreshWorldGenPackDescription();

  DecorationDensityField = AddSlider(panel, "Decoration density:",
                                     DecorationDensityLabel, 0.f, 2.f, 0.05f, 2);
  GroundCoverBox = AddCheckbox(panel, "Ground cover",
                               [this](bool v) { FormSettings.EnableGroundCover = v; });
  DecorationBox = AddCheckbox(panel, "Decoration",
                              [this](bool v) { FormSettings.EnableDecoration = v; });
  OreDensityField =
      AddSlider(panel, "Ore density:", OreDensityLabel, 0.f, 2.f, 0.05f, 2);
  TerrainErosionField =
      AddSlider(panel, "Terrain erosion:", TerrainErosionLabel, 0.f, 1.f, 0.01f, 2);
  BiomeBlendField =
      AddSlider(panel, "Biome blend radius:", BiomeBlendLabel, 0.f, 16.f, 1.f, 0);
  BiomeForestField = AddSlider(panel, "Forest biome weight:", BiomeForestLabel,
                               0.f, 2.f, 0.05f, 2);
  BiomeDesertField = AddSlider(panel, "Desert biome weight:", BiomeDesertLabel,
                               0.f, 2.f, 0.05f, 2);
  BiomePlainsField = AddSlider(panel, "Plains biome weight:", BiomePlainsLabel,
                               0.f, 2.f, 0.05f, 2);
  BiomeHillsField = AddSlider(panel, "Hills biome weight:", BiomeHillsLabel, 0.f,
                              2.f, 0.05f, 2);
  BiomeTundraField = AddSlider(panel, "Tundra biome weight:", BiomeTundraLabel,
                               0.f, 2.f, 0.05f, 2);

  CaveMinYField = AddSlider(panel, "Cave min Y:", CaveMinYLabel, -64.f, 120.f,
                            1.f, 0);
  CaveMaxDepthField = AddSlider(panel, "Cave max depth below surface:",
                                CaveMaxDepthLabel, 0.f, 96.f, 1.f, 0);
  CaveThresholdField =
      AddSlider(panel, "Cave threshold (legacy):", CaveThresholdLabel, 0.f, 1.f,
                0.01f, 2);
  CaveScaleField = AddSlider(panel, "Cave noise scale (legacy):", CaveScaleLabel,
                             0.01f, 0.2f, 0.005f, 3);
  CaveStyleLabel = AddLabel(panel, "Cave style (legacy):");
  auto caveStyle = std::make_unique<UGuiListView>(Theme);
  CaveStyleList = caveStyle.get();
  caveStyle->SetItems({"Noise", "Worm"});
  caveStyle->SetVisibleRowCount(2);
  caveStyle->SetSelectedIndex(FormSettings.Caves.style == CaveStyle::Worm ? 1 : 0);
  panel.AddChild(std::move(caveStyle));

  RavinesBox = AddCheckbox(panel, "Ravines (shallow dells)",
                           [this](bool v) { FormSettings.Ravines.enabled = v; });
  RavineRarityField = AddSlider(panel, "Ravine rarity (higher = rarer):",
                                RavineRarityLabel, 100.f, 2000.f, 50.f, 0);
  RavineAquaticMaxDepthField =
      AddSlider(panel, "Ravine max depth in water:", RavineAquaticMaxDepthLabel,
                0.f, 16.f, 1.f, 0);
  RavineFillWaterBox =
      AddCheckbox(panel, "Fill ravines with water",
                  [this](bool v) { FormSettings.Ravines.fillWater = v; });
  LavaBox = AddCheckbox(panel, "Fill lava",
                        [this](bool v) { FormSettings.FillLava = v; });
  FireBox = AddCheckbox(panel, "Fill fire",
                        [this](bool v) { FormSettings.FillFire = v; });

  TerrainQualityLabel = AddLabel(panel, "Terrain quality:");
  UnifiedHeightFieldBox = AddCheckbox(
      panel, "Unified height field (legacy)",
      [this](bool v) { FormSettings.Tuning.useUnifiedHeightField = v; });
  WireTerrainQualityHint(
      *UnifiedHeightFieldBox,
      "Legacy heightmap pipeline: blend biome height profiles into one "
      "continuous surface before coast and erosion passes.");
  AnalyticValleysBox = AddCheckbox(
      panel, "Rivers and valleys",
      [this](bool v) { FormSettings.Tuning.useAnalyticValleys = v; });
  WireTerrainQualityHint(
      *AnalyticValleysBox,
      "Meandering river valleys carved on lowlands. Off = no rivers carved "
      "after the terrain pass.");
  MudflowErosionBox = AddCheckbox(
      panel, "Mudflow erosion",
      [this](bool v) { FormSettings.Tuning.useMudflowErosion = v; });
  WireTerrainQualityHint(
      *MudflowErosionBox,
      "Chunk post-pass: moves dirt/gravel/sand one block down steep slopes "
      "(max 2 iterations) before fluid fill.");
  DensityRefineParityBox = AddCheckbox(
      panel, "Density refine parity (legacy)",
      [this](bool v) { FormSettings.Tuning.useDensityRefineParity = v; });
  WireTerrainQualityHint(
      *DensityRefineParityBox,
      "Worldgen profiles before 3 only: apply the heightmap refinement chain "
      "(rivers, coast, smoothing) to the 3D density surface.");
  TerrainQualityHintLabel =
      AddLabel(panel, "Hover a terrain quality option above for details.");
}

std::vector<GuiGridItem> UWorldGenSettingsForm::BuildGridItems() const
{
  std::vector<GuiGridItem> items;
  int row = 0;
  const auto shown = [](const UGuiWidget *w) { return w && w->IsVisible(); };
  // One label + one control per row.
  const auto pair = [&](UGuiWidget *label, UGuiWidget *control, int minH)
  {
    if (!shown(label) && !shown(control))
    {
      return;
    }
    items.push_back({label, row, 0, 1, 1, 28});
    items.push_back({control, row, 1, 1, 1, minH});
    ++row;
  };
  // A widget spanning both columns.
  const auto span = [&](UGuiWidget *w, int minH)
  {
    if (!shown(w))
    {
      return;
    }
    items.push_back({w, row, 0, 1, 2, minH});
    ++row;
  };
  // Two checkboxes side by side.
  const auto twin = [&](UGuiWidget *a, UGuiWidget *b)
  {
    if (!shown(a) && !shown(b))
    {
      return;
    }
    items.push_back({a, row, 0, 1, 1, 30});
    items.push_back({b, row, 1, 1, 1, 30});
    ++row;
  };

  span(HintLabel, 28);
  span(TabBar, 30);

  // Basic tab.
  pair(GeneratorCaption, GeneratorList, 150);
  span(GeneratorDescLabel, 36);
  pair(PresetLabel, PresetList, 108);
  pair(SeedLabel, SeedInput, 32);
  pair(FlatYLabel, FlatYField, 32);
  pair(TerrainRoughnessLabel, TerrainRoughnessField, 32);
  pair(VegetationDensityLabel, VegetationDensityField, 32);
  pair(StructureDensityLabel, StructureDensityField, 32);
  twin(TreesBox, StructuresBox);
  twin(CavesBox, OresBox);
  twin(WaterBox, nullptr);

  // Advanced tab.
  pair(SeaLevelLabel, SeaLevelField, 32);
  pair(MaxHeightLabel, MaxHeightField, 32);
  pair(BedrockTopYLabel, BedrockTopYField, 32);
  pair(TerrainBackendLabel, TerrainBackendList, 72);
  pair(WorldGenPackIdLabel, WorldGenPackList, 72);
  span(WorldGenPackDescLabel, 36);
  pair(DecorationDensityLabel, DecorationDensityField, 32);
  twin(GroundCoverBox, DecorationBox);
  pair(OreDensityLabel, OreDensityField, 32);
  pair(TerrainErosionLabel, TerrainErosionField, 32);
  pair(BiomeBlendLabel, BiomeBlendField, 32);
  pair(BiomeForestLabel, BiomeForestField, 32);
  pair(BiomeDesertLabel, BiomeDesertField, 32);
  pair(BiomePlainsLabel, BiomePlainsField, 32);
  pair(BiomeHillsLabel, BiomeHillsField, 32);
  pair(BiomeTundraLabel, BiomeTundraField, 32);
  pair(CaveMinYLabel, CaveMinYField, 32);
  pair(CaveMaxDepthLabel, CaveMaxDepthField, 32);
  pair(CaveThresholdLabel, CaveThresholdField, 32);
  pair(CaveScaleLabel, CaveScaleField, 32);
  pair(CaveStyleLabel, CaveStyleList, 54);
  span(RavinesBox, 30);
  pair(RavineRarityLabel, RavineRarityField, 32);
  pair(RavineAquaticMaxDepthLabel, RavineAquaticMaxDepthField, 32);
  span(RavineFillWaterBox, 30);
  twin(LavaBox, FireBox);
  span(TerrainQualityLabel, 28);
  twin(UnifiedHeightFieldBox, DensityRefineParityBox);
  twin(AnalyticValleysBox, MudflowErosionBox);
  span(TerrainQualityHintLabel, 48);
  return items;
}

} // namespace cutum
