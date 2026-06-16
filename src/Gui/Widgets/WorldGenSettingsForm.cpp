#include "Gui/Widgets/WorldGenSettingsForm.h"
#include "Gui/Core/GuiTheme.h"
#include "Gui/Widgets/GuiButton.h"
#include "Gui/Widgets/GuiCheckbox.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Widgets/GuiTextInput.h"
#include <algorithm>
#include <sstream>

namespace cutum
{

namespace
{

std::string GeneratorLabel(ProceduralGenerator g)
{
  return ProceduralGeneratorToString(g);
}

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

} // namespace

UWorldGenSettingsForm::UWorldGenSettingsForm(const GuiTheme *theme)
    : Theme(theme)
{
}

void UWorldGenSettingsForm::SetSettings(const ProceduralSettings &settings)
{
  FormSettings = settings;
  if (HintLabel)
  {
    HintLabel->SetText("Defaults for the next new worlds.");
  }
  if (GeneratorBtn)
  {
    GeneratorBtn->SetLabel(GeneratorLabel(FormSettings.Generator));
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
  ApplyGeneratorTierDefaults(s);
  return s;
}

void UWorldGenSettingsForm::CycleGenerator()
{
  static constexpr ProceduralGenerator kOrder[] = {
      ProceduralGenerator::Flat,          ProceduralGenerator::Heightmap,
      ProceduralGenerator::Overworld,     ProceduralGenerator::Hills,
      ProceduralGenerator::Mountains,     ProceduralGenerator::OverworldBiomes,
      ProceduralGenerator::OverworldFull,
  };
  int idx = 0;
  for (int i = 0; i < static_cast<int>(sizeof(kOrder) / sizeof(kOrder[0])); ++i)
  {
    if (kOrder[i] == FormSettings.Generator)
    {
      idx = i;
      break;
    }
  }
  idx = (idx + 1) % static_cast<int>(sizeof(kOrder) / sizeof(kOrder[0]));
  FormSettings.Generator = kOrder[idx];
  if (GeneratorBtn)
  {
    GeneratorBtn->SetLabel(GeneratorLabel(FormSettings.Generator));
  }
}

void UWorldGenSettingsForm::CycleVertical()
{
  FormSettings.Vertical = FormSettings.Vertical == VerticalMode::Compact
                              ? VerticalMode::Extended
                              : VerticalMode::Compact;
  if (VerticalBtn)
  {
    VerticalBtn->SetLabel(VerticalLabel(FormSettings.Vertical));
  }
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
  auto genBtn = std::make_unique<UGuiButton>(
      Theme, GeneratorLabel(FormSettings.Generator));
  GeneratorBtn = genBtn.get();
  genBtn->SetOnClick([this]() { CycleGenerator(); });
  panel.AddChild(std::move(genBtn));

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
  items.push_back({GeneratorBtn, 1, 1, 1, 1, 32});
  items.push_back({VerticalCaption, 2, 0, 1, 1, 28});
  items.push_back({VerticalBtn, 2, 1, 1, 1, 32});
  items.push_back({SeedLabel, 3, 0, 1, 1, 28});
  items.push_back({SeedInput, 3, 1, 1, 1, 32});
  items.push_back({SeaLevelLabel, 4, 0, 1, 1, 28});
  items.push_back({SeaLevelInput, 4, 1, 1, 1, 32});
  items.push_back({MaxHeightLabel, 5, 0, 1, 1, 28});
  items.push_back({MaxHeightInput, 5, 1, 1, 1, 32});
  items.push_back({FlatYLabel, 6, 0, 1, 1, 28});
  items.push_back({FlatYInput, 6, 1, 1, 1, 32});

  items.push_back({CavesBox, 7, 0, 1, 1, 30});
  items.push_back({TreesBox, 7, 1, 1, 1, 30});
  items.push_back({WaterBox, 8, 0, 1, 1, 30});
  items.push_back({LavaBox, 8, 1, 1, 1, 30});
  items.push_back({FireBox, 9, 0, 1, 2, 30});
  return items;
}

} // namespace cutum
