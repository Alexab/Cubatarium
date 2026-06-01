#include "WorldGenSettingsForm.h"
#include "GuiButton.h"
#include "GuiCheckbox.h"
#include "GuiLabel.h"
#include "GuiPanel.h"
#include "GuiTextInput.h"
#include "Gui/GuiTheme.h"
#include <algorithm>
#include <sstream>

namespace cutum {

namespace {

std::string GeneratorLabel(ProceduralGenerator g)
{
    return ProceduralGeneratorToString(g);
}

std::string VerticalLabel(VerticalMode m)
{
    return VerticalModeToString(m);
}

int ParseIntOr(const std::string& text, int fallback)
{
    try {
        return std::stoi(text);
    } catch (...) {
        return fallback;
    }
}

uint32_t ParseSeedOr(const std::string& text, uint32_t fallback)
{
    try {
        return static_cast<uint32_t>(std::stoul(text));
    } catch (...) {
        return fallback;
    }
}

} // namespace

WorldGenSettingsForm::WorldGenSettingsForm(const GuiTheme* theme)
    : theme_(theme)
{
}

void WorldGenSettingsForm::SetSettings(const ProceduralSettings& settings)
{
    settings_ = settings;
    if (hintLabel_) {
        hintLabel_->SetText("Defaults for the next new worlds.");
    }
    if (generatorBtn_) {
        generatorBtn_->SetLabel(GeneratorLabel(settings_.generator));
    }
    if (verticalBtn_) {
        verticalBtn_->SetLabel(VerticalLabel(settings_.vertical));
    }
    if (seedInput_) {
        seedInput_->SetText(std::to_string(settings_.seed));
    }
    if (seaLevelInput_) {
        seaLevelInput_->SetText(std::to_string(settings_.seaLevel));
    }
    if (maxHeightInput_) {
        maxHeightInput_->SetText(std::to_string(settings_.maxHeight));
    }
    if (flatYInput_) {
        flatYInput_->SetText(std::to_string(settings_.flatSurfaceY));
    }
    if (cavesBox_) {
        cavesBox_->SetChecked(settings_.enableCaves);
    }
    if (treesBox_) {
        treesBox_->SetChecked(settings_.enableTrees);
    }
    if (waterBox_) {
        waterBox_->SetChecked(settings_.fillWater);
    }
    if (lavaBox_) {
        lavaBox_->SetChecked(settings_.fillLava);
    }
    if (fireBox_) {
        fireBox_->SetChecked(settings_.fillFire);
    }
}

ProceduralSettings WorldGenSettingsForm::ReadSettings() const
{
    ProceduralSettings s = settings_;
    if (seedInput_) {
        s.seed = ParseSeedOr(seedInput_->GetText(), s.seed);
    }
    if (seaLevelInput_) {
        s.seaLevel = ParseIntOr(seaLevelInput_->GetText(), s.seaLevel);
    }
    if (maxHeightInput_) {
        s.maxHeight = ParseIntOr(maxHeightInput_->GetText(), s.maxHeight);
    }
    if (flatYInput_) {
        s.flatSurfaceY = ParseIntOr(flatYInput_->GetText(), s.flatSurfaceY);
    }
    if (cavesBox_) {
        s.enableCaves = cavesBox_->IsChecked();
    }
    if (treesBox_) {
        s.enableTrees = treesBox_->IsChecked();
    }
    if (waterBox_) {
        s.fillWater = waterBox_->IsChecked();
    }
    if (lavaBox_) {
        s.fillLava = lavaBox_->IsChecked();
    }
    if (fireBox_) {
        s.fillFire = fireBox_->IsChecked();
    }
    ResolveProceduralDefaults(s);
    ApplyGeneratorTierDefaults(s);
    return s;
}

void WorldGenSettingsForm::CycleGenerator()
{
    static constexpr ProceduralGenerator kOrder[] = {
        ProceduralGenerator::Flat,
        ProceduralGenerator::Heightmap,
        ProceduralGenerator::Overworld,
        ProceduralGenerator::Hills,
        ProceduralGenerator::Mountains,
        ProceduralGenerator::OverworldBiomes,
        ProceduralGenerator::OverworldFull,
    };
    int idx = 0;
    for (int i = 0; i < static_cast<int>(sizeof(kOrder) / sizeof(kOrder[0])); ++i) {
        if (kOrder[i] == settings_.generator) {
            idx = i;
            break;
        }
    }
    idx = (idx + 1) % static_cast<int>(sizeof(kOrder) / sizeof(kOrder[0]));
    settings_.generator = kOrder[idx];
    if (generatorBtn_) {
        generatorBtn_->SetLabel(GeneratorLabel(settings_.generator));
    }
}

void WorldGenSettingsForm::CycleVertical()
{
    settings_.vertical = settings_.vertical == VerticalMode::Compact ? VerticalMode::Extended
                                                                     : VerticalMode::Compact;
    if (verticalBtn_) {
        verticalBtn_->SetLabel(VerticalLabel(settings_.vertical));
    }
}

void WorldGenSettingsForm::BuildInto(GuiPanel& panel)
{
    if (!built_) {
        AddWidgetsTo(panel);
        built_ = true;
    }
    SetSettings(settings_);
}

void WorldGenSettingsForm::AddWidgetsTo(GuiPanel& panel)
{
    auto hint = std::make_unique<GuiLabel>(theme_, "Defaults for the next new worlds.");
    hintLabel_ = hint.get();
    panel.AddChild(std::move(hint));

    auto genLabel = std::make_unique<GuiLabel>(theme_, "Generator:");
    panel.AddChild(std::move(genLabel));
    auto genBtn = std::make_unique<GuiButton>(theme_, GeneratorLabel(settings_.generator));
    generatorBtn_ = genBtn.get();
    genBtn->SetOnClick([this]() { CycleGenerator(); });
    panel.AddChild(std::move(genBtn));

    auto vertLabel = std::make_unique<GuiLabel>(theme_, "Vertical:");
    panel.AddChild(std::move(vertLabel));
    auto vertBtn = std::make_unique<GuiButton>(theme_, VerticalLabel(settings_.vertical));
    verticalBtn_ = vertBtn.get();
    vertBtn->SetOnClick([this]() { CycleVertical(); });
    panel.AddChild(std::move(vertBtn));

    auto seedLabel = std::make_unique<GuiLabel>(theme_, "World seed:");
    panel.AddChild(std::move(seedLabel));
    auto seedIn = std::make_unique<GuiTextInput>(theme_);
    seedInput_ = seedIn.get();
    seedIn->SetText(std::to_string(settings_.seed));
    panel.AddChild(std::move(seedIn));

    auto seaLabel = std::make_unique<GuiLabel>(theme_, "Sea level:");
    panel.AddChild(std::move(seaLabel));
    auto seaIn = std::make_unique<GuiTextInput>(theme_);
    seaLevelInput_ = seaIn.get();
    seaIn->SetText(std::to_string(settings_.seaLevel));
    panel.AddChild(std::move(seaIn));

    auto maxLabel = std::make_unique<GuiLabel>(theme_, "Max height:");
    panel.AddChild(std::move(maxLabel));
    auto maxIn = std::make_unique<GuiTextInput>(theme_);
    maxHeightInput_ = maxIn.get();
    maxIn->SetText(std::to_string(settings_.maxHeight));
    panel.AddChild(std::move(maxIn));

    auto flatLabel = std::make_unique<GuiLabel>(theme_, "Flat surface Y:");
    panel.AddChild(std::move(flatLabel));
    auto flatIn = std::make_unique<GuiTextInput>(theme_);
    flatYInput_ = flatIn.get();
    flatIn->SetText(std::to_string(settings_.flatSurfaceY));
    panel.AddChild(std::move(flatIn));

    auto caves = std::make_unique<GuiCheckbox>(theme_, "Caves");
    cavesBox_ = caves.get();
    caves->SetChecked(settings_.enableCaves);
    caves->SetOnChanged([this](bool v) { settings_.enableCaves = v; });
    panel.AddChild(std::move(caves));

    auto trees = std::make_unique<GuiCheckbox>(theme_, "Trees");
    treesBox_ = trees.get();
    trees->SetChecked(settings_.enableTrees);
    trees->SetOnChanged([this](bool v) { settings_.enableTrees = v; });
    panel.AddChild(std::move(trees));

    auto water = std::make_unique<GuiCheckbox>(theme_, "Fill water");
    waterBox_ = water.get();
    water->SetChecked(settings_.fillWater);
    water->SetOnChanged([this](bool v) { settings_.fillWater = v; });
    panel.AddChild(std::move(water));

    auto lava = std::make_unique<GuiCheckbox>(theme_, "Fill lava");
    lavaBox_ = lava.get();
    lava->SetChecked(settings_.fillLava);
    lava->SetOnChanged([this](bool v) { settings_.fillLava = v; });
    panel.AddChild(std::move(lava));

    auto fire = std::make_unique<GuiCheckbox>(theme_, "Fill fire");
    fireBox_ = fire.get();
    fire->SetChecked(settings_.fillFire);
    fire->SetOnChanged([this](bool v) { settings_.fillFire = v; });
    panel.AddChild(std::move(fire));
}

} // namespace cutum
