#include "WorldGenSettingsForm.h"
#include "Gui/Core/GuiTheme.h"
#include "GuiButton.h"
#include "GuiCheckbox.h"
#include "GuiLabel.h"
#include "GuiPanel.h"
#include "GuiTextInput.h"
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
    : theme_(theme)
{
}

void UWorldGenSettingsForm::SetSettings(const ProceduralSettings &settings)
{
  settings_ = settings;
  if (hintLabel_)
  {
    hintLabel_->SetText("Defaults for the next new worlds.");
  }
  if (generatorBtn_)
  {
    generatorBtn_->SetLabel(GeneratorLabel(settings_.generator));
  }
  if (verticalBtn_)
  {
    verticalBtn_->SetLabel(VerticalLabel(settings_.vertical));
  }
  if (seedInput_)
  {
    seedInput_->SetText(std::to_string(settings_.seed));
  }
  if (seaLevelInput_)
  {
    seaLevelInput_->SetText(std::to_string(settings_.seaLevel));
  }
  if (maxHeightInput_)
  {
    maxHeightInput_->SetText(std::to_string(settings_.maxHeight));
  }
  if (flatYInput_)
  {
    flatYInput_->SetText(std::to_string(settings_.flatSurfaceY));
  }
  if (cavesBox_)
  {
    cavesBox_->SetChecked(settings_.enableCaves);
  }
  if (treesBox_)
  {
    treesBox_->SetChecked(settings_.enableTrees);
  }
  if (waterBox_)
  {
    waterBox_->SetChecked(settings_.fillWater);
  }
  if (lavaBox_)
  {
    lavaBox_->SetChecked(settings_.fillLava);
  }
  if (fireBox_)
  {
    fireBox_->SetChecked(settings_.fillFire);
  }
}

ProceduralSettings UWorldGenSettingsForm::ReadSettings() const
{
  ProceduralSettings s = settings_;
  if (seedInput_)
  {
    s.seed = ParseSeedOr(seedInput_->GetText(), s.seed);
  }
  if (seaLevelInput_)
  {
    s.seaLevel = ParseIntOr(seaLevelInput_->GetText(), s.seaLevel);
  }
  if (maxHeightInput_)
  {
    s.maxHeight = ParseIntOr(maxHeightInput_->GetText(), s.maxHeight);
  }
  if (flatYInput_)
  {
    s.flatSurfaceY = ParseIntOr(flatYInput_->GetText(), s.flatSurfaceY);
  }
  if (cavesBox_)
  {
    s.enableCaves = cavesBox_->IsChecked();
  }
  if (treesBox_)
  {
    s.enableTrees = treesBox_->IsChecked();
  }
  if (waterBox_)
  {
    s.fillWater = waterBox_->IsChecked();
  }
  if (lavaBox_)
  {
    s.fillLava = lavaBox_->IsChecked();
  }
  if (fireBox_)
  {
    s.fillFire = fireBox_->IsChecked();
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
    if (kOrder[i] == settings_.generator)
    {
      idx = i;
      break;
    }
  }
  idx = (idx + 1) % static_cast<int>(sizeof(kOrder) / sizeof(kOrder[0]));
  settings_.generator = kOrder[idx];
  if (generatorBtn_)
  {
    generatorBtn_->SetLabel(GeneratorLabel(settings_.generator));
  }
}

void UWorldGenSettingsForm::CycleVertical()
{
  settings_.vertical = settings_.vertical == VerticalMode::Compact
                           ? VerticalMode::Extended
                           : VerticalMode::Compact;
  if (verticalBtn_)
  {
    verticalBtn_->SetLabel(VerticalLabel(settings_.vertical));
  }
}

void UWorldGenSettingsForm::BuildInto(UGuiPanel &panel)
{
  if (!built_)
  {
    AddWidgetsTo(panel);
    built_ = true;
  }
  SetSettings(settings_);
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
      std::make_unique<UGuiLabel>(theme_, "Defaults for the next new worlds.");
  hintLabel_ = hint.get();
  panel.AddChild(std::move(hint));

  auto genLabel = std::make_unique<UGuiLabel>(theme_, "Generator:");
  generatorLabel_ = genLabel.get();
  panel.AddChild(std::move(genLabel));
  auto genBtn =
      std::make_unique<UGuiButton>(theme_, GeneratorLabel(settings_.generator));
  generatorBtn_ = genBtn.get();
  genBtn->SetOnClick([this]() { CycleGenerator(); });
  panel.AddChild(std::move(genBtn));

  auto vertLabel = std::make_unique<UGuiLabel>(theme_, "Vertical:");
  verticalLabel_ = vertLabel.get();
  panel.AddChild(std::move(vertLabel));
  auto vertBtn =
      std::make_unique<UGuiButton>(theme_, VerticalLabel(settings_.vertical));
  verticalBtn_ = vertBtn.get();
  vertBtn->SetOnClick([this]() { CycleVertical(); });
  panel.AddChild(std::move(vertBtn));

  auto seedLabel = std::make_unique<UGuiLabel>(theme_, "World seed:");
  seedLabel_ = seedLabel.get();
  panel.AddChild(std::move(seedLabel));
  auto seedIn = std::make_unique<UGuiTextInput>(theme_);
  seedInput_ = seedIn.get();
  seedIn->SetText(std::to_string(settings_.seed));
  panel.AddChild(std::move(seedIn));

  auto seaLabel = std::make_unique<UGuiLabel>(theme_, "Sea level:");
  seaLevelLabel_ = seaLabel.get();
  panel.AddChild(std::move(seaLabel));
  auto seaIn = std::make_unique<UGuiTextInput>(theme_);
  seaLevelInput_ = seaIn.get();
  seaIn->SetText(std::to_string(settings_.seaLevel));
  panel.AddChild(std::move(seaIn));

  auto maxLabel = std::make_unique<UGuiLabel>(theme_, "Max height:");
  maxHeightLabel_ = maxLabel.get();
  panel.AddChild(std::move(maxLabel));
  auto maxIn = std::make_unique<UGuiTextInput>(theme_);
  maxHeightInput_ = maxIn.get();
  maxIn->SetText(std::to_string(settings_.maxHeight));
  panel.AddChild(std::move(maxIn));

  auto flatLabel = std::make_unique<UGuiLabel>(theme_, "Flat surface Y:");
  flatYLabel_ = flatLabel.get();
  panel.AddChild(std::move(flatLabel));
  auto flatIn = std::make_unique<UGuiTextInput>(theme_);
  flatYInput_ = flatIn.get();
  flatIn->SetText(std::to_string(settings_.flatSurfaceY));
  panel.AddChild(std::move(flatIn));

  auto caves = std::make_unique<UGuiCheckbox>(theme_, "Caves");
  cavesBox_ = caves.get();
  caves->SetChecked(settings_.enableCaves);
  caves->SetOnChanged([this](bool v) { settings_.enableCaves = v; });
  panel.AddChild(std::move(caves));

  auto trees = std::make_unique<UGuiCheckbox>(theme_, "Trees");
  treesBox_ = trees.get();
  trees->SetChecked(settings_.enableTrees);
  trees->SetOnChanged([this](bool v) { settings_.enableTrees = v; });
  panel.AddChild(std::move(trees));

  auto water = std::make_unique<UGuiCheckbox>(theme_, "Fill water");
  waterBox_ = water.get();
  water->SetChecked(settings_.fillWater);
  water->SetOnChanged([this](bool v) { settings_.fillWater = v; });
  panel.AddChild(std::move(water));

  auto lava = std::make_unique<UGuiCheckbox>(theme_, "Fill lava");
  lavaBox_ = lava.get();
  lava->SetChecked(settings_.fillLava);
  lava->SetOnChanged([this](bool v) { settings_.fillLava = v; });
  panel.AddChild(std::move(lava));

  auto fire = std::make_unique<UGuiCheckbox>(theme_, "Fill fire");
  fireBox_ = fire.get();
  fire->SetChecked(settings_.fillFire);
  fire->SetOnChanged([this](bool v) { settings_.fillFire = v; });
  panel.AddChild(std::move(fire));
}

std::vector<GuiGridItem> UWorldGenSettingsForm::BuildGridItems() const
{
  std::vector<GuiGridItem> items;
  items.push_back({hintLabel_, 0, 0, 1, 2, 28});

  items.push_back({generatorLabel_, 1, 0, 1, 1, 28});
  items.push_back({generatorBtn_, 1, 1, 1, 1, 32});
  items.push_back({verticalLabel_, 2, 0, 1, 1, 28});
  items.push_back({verticalBtn_, 2, 1, 1, 1, 32});
  items.push_back({seedLabel_, 3, 0, 1, 1, 28});
  items.push_back({seedInput_, 3, 1, 1, 1, 32});
  items.push_back({seaLevelLabel_, 4, 0, 1, 1, 28});
  items.push_back({seaLevelInput_, 4, 1, 1, 1, 32});
  items.push_back({maxHeightLabel_, 5, 0, 1, 1, 28});
  items.push_back({maxHeightInput_, 5, 1, 1, 1, 32});
  items.push_back({flatYLabel_, 6, 0, 1, 1, 28});
  items.push_back({flatYInput_, 6, 1, 1, 1, 32});

  items.push_back({cavesBox_, 7, 0, 1, 1, 30});
  items.push_back({treesBox_, 7, 1, 1, 1, 30});
  items.push_back({waterBox_, 8, 0, 1, 1, 30});
  items.push_back({lavaBox_, 8, 1, 1, 1, 30});
  items.push_back({fireBox_, 9, 0, 1, 2, 30});
  return items;
}

} // namespace cutum
