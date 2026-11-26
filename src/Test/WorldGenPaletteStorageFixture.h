#pragma once

#include "Gui/Preview/ContentPreviewDock.h"
#include "Gui/Widgets/GuiButton.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/GuiScrollView.h"
#include "Gui/Widgets/GuiSlot.h"
#include "Gui/Widgets/GuiTabBar.h"
#include "Render/Textures/TextureCube.h"
#include "Test/ApplicationOperationStorageFixture.h"
#include "Test/GuiContextStorageFixture.h"
#include "Test/OrdinaryNewObservation.h"
#include "World/Core/World.h"

namespace cutum
{
struct WorldGenPaletteStorageProbe
{
  static UGuiTabBar &Tabs(UWorldGenPaletteScreen &screen) { return *screen.MainTabs; }
  static UGuiTabBar &Sections(UWorldGenPaletteScreen &screen) { return *screen.SectionTabs; }
  static UGuiScrollView &Content(UWorldGenPaletteScreen &screen) { return *screen.ContentScroll; }
  static UGuiScrollView &Picker(UWorldGenPaletteScreen &screen) { return *screen.PickerScroll; }
  static UGuiButton &Add(UWorldGenPaletteScreen &screen) { return *screen.AddButton; }
  static bool PickerVisible(const UWorldGenPaletteScreen &screen) { return screen.PickerPanel->IsVisible(); }
  static const UGuiLabel &Title(const UWorldGenPaletteScreen &screen) { return *screen.PickerTitleLabel; }
  static const UGuiPanel &Preview(const UWorldGenPaletteScreen &screen) { return *screen.PreviewDock->GetPanel(); }
  static bool Capture(const UWorldGenPaletteScreen &screen) { return screen.PointerPressed; }
};

namespace worldgen_palette_storage_fixture
{
using gui_context_fixture::Require;
using Json = nlohmann::json;
using Probe = WorldGenPaletteStorageProbe;

inline const std::string ObjectId = "palette_fixture_object_with_long_identity";
inline const std::string BlockId = "palette_fixture_block_with_long_identity";
inline const std::string Section = "palette_fixture_section_with_long_identity";

class Catalog final : public IUContentCatalog
{
public:
  bool Replacement{false};
  size_t ExtraObjects{0};

  std::vector<std::string> GetTypeIds(ContentKind) const override { return {"fixture"}; }
  std::string GetTypeDisplayName(const std::string &) const override { return "Fixture"; }
  std::vector<CatalogEntry> GetEntries(ContentKind kind, const std::string &) const override
  {
    if (kind == ContentKind::Block)
      return {{BlockId, "A long fixture block display name"}};
    std::vector<CatalogEntry> entries{
        {ObjectId, Replacement ? "A replacement fixture display name" : "A long fixture object display name"}};
    for (size_t i = 0; i < ExtraObjects; ++i)
      entries.push_back({"entry_" + std::to_string(i), "Extra entry number " + std::to_string(i)});
    return entries;
  }
};

inline const std::vector<std::unique_ptr<UGuiWidget>> &Rows(const UGuiScrollView &scroll)
{
  return scroll.Content().GetChildren();
}

inline GuiMouseEvent Point(const UGuiWidget &widget)
{
  const auto bounds = widget.GetBounds();
  GuiMouseEvent event;
  event.Button = GuiMouseButton::Left;
  event.X = bounds.X + 2;
  event.Y = bounds.Y + 2;
  return event;
}

inline Json CheckPublication(UGuiContext &context, UWorld &world)
{
  Json cases = Json::object();
  for (const std::string phase : {"shell", "objects", "terrain", "picker_replace"})
  {
    for (size_t cutoff = 0; ; ++cutoff)
    {
      Catalog catalog;
      auto screen = std::make_unique<UWorldGenPaletteScreen>(&world, &catalog, nullptr, nullptr);
      screen->OnAttach(context);
      screen->Build(context);
      screen->SetVisible(true);
      screen->Update(0);
      auto &tabs = Probe::Tabs(*screen);
      const bool picker = phase == "picker_replace";
      const bool shell = phase == "shell";
      const bool objects = phase == "objects";
      if (picker)
      {
        Require(Probe::Add(*screen).Activate(), "object picker has an add action");
        screen->Update(0);
        catalog.Replacement = true;
        Require(Probe::Add(*screen).Activate(), "queue replacement of visible object picker");
      }
      else if (objects)
        screen->SetVisible(true);
      else if (!shell)
      {
        auto tab = Point(tabs);
        tab.X += tabs.GetBounds().W / 3;
        Require(tabs.OnMouseDown(tab), "queue terrain tab");
      }
      const auto *previousRoot = screen->GetRoot();
      const auto *previousRow = Rows(picker ? Probe::Picker(*screen) : Probe::Content(*screen)).front().get();
      const auto previousTitle = Probe::Title(*screen).GetText();
      bool refused = false;
      size_t live = 0;
      size_t peak = 0;
      const auto perform = [&]
      {
        if (shell)
        {
          try { screen->Build(context); }
          catch (const std::bad_alloc &) {}
        }
        else
          screen->Update(0);
      };
      {
        observation::Scope observed(cutoff);
        perform();
        refused = observation::State.refusals > 0;
        observation::State.fail_after = SIZE_MAX;
        if (refused)
        {
          Require(screen->GetRoot() == previousRoot && Probe::Tabs(*screen).GetActiveTab() == 0 &&
                      Rows(picker ? Probe::Picker(*screen) : Probe::Content(*screen)).front().get() == previousRow &&
                      Probe::PickerVisible(*screen) == picker && Probe::Title(*screen).GetText() == previousTitle &&
                      !observation::State.live,
                  "refusal preserves published root, tab, row and picker title");
          perform();
        }
        live = observation::State.live;
        peak = observation::State.peak;
        if (shell)
          Require(screen->GetRoot() != previousRoot, "complete shell replaces prior shell");
        else
        {
          const auto requests = observation::State.attempts;
          const auto storage = screen->InspectGridStorage();
          Require(storage.supported && requests == observation::State.attempts,
                  "grid storage inspection matches the current owner without allocating");
          if (picker)
            Require(Rows(Probe::Picker(*screen)).front().get() != previousRow &&
                        Probe::PickerVisible(*screen) && storage.pickerBytes + storage.pickerTitleBytes == live &&
                        static_cast<const UGuiSlot &>(*Rows(Probe::Picker(*screen)).front()).GetLabel() ==
                            "A replacement fixture display name",
                    "picker retry publishes its exact new rows and backing");
          else
            Require(Probe::Tabs(*screen).GetActiveTab() == (objects ? 0 : 1) &&
                        Rows(Probe::Content(*screen)).size() == (objects ? 1 : 5) &&
                        storage.contentBytes + storage.sectionTabBytes == live,
                    "main grid retry publishes exact rows and section backing");
        }
        screen.reset();
        Require(!observation::State.live && !observation::State.blocks, "all prepared grid backing retires");
      }
      if (!refused)
      {
        Require(cutoff > 0, "fixture observes allocation prefixes");
        cases[phase] = {{"refusals", cutoff}, {"peak", peak}, {"retained", live}};
        break;
      }
    }
  }
  return cases;
}

inline void Click(UApplication &app, const UGuiWidget &widget, const char *message)
{
  const auto at = Point(widget);
  Require(app.RouteMouseButton(0, true, at.X, at.Y) && app.RouteMouseButton(0, false, at.X, at.Y),
          message);
}

inline Json Run()
{
  auto world = std::make_shared<UWorld>(std::make_shared<UTextureCubeStorage>(nullptr), nullptr);
  Require(world->GetWorldFolderPath().empty(), "palette fixture does not write a player's world");
  WorldGenObjectEntry planted;
  planted.ObjectName = ObjectId;
  world->GetWorldGenSets().VegetationSections[Section].Entries.push_back(std::move(planted));
  Catalog catalog;
  UApplication app(nullptr, world, nullptr, nullptr, nullptr, nullptr, nullptr);
  Require(app.GetGui().Initialize(std::make_shared<UShaderManager>(), {}), "initialize palette application GUI");
  ApplicationOperationSourceProbe::SetGameState(app);
  const auto prefixes = CheckPublication(app.GetGui(), *world);
  auto &screen = ApplicationOperationSourceProbe::OpenWorldGen(app, catalog);
  screen.OnViewportChanged(1280, 768);
  screen.Update(0);
  Require(Probe::Sections(screen).GetTabLabels() == std::vector<std::string>{Section},
          "object view publishes its named section");
  Click(app, Probe::Add(screen), "open initial object picker");
  screen.Update(0);
  auto &picker = Probe::Picker(screen);
  const auto *activeRow = Rows(picker).front().get();
  const auto rowPoint = Point(*activeRow);
  Require(app.RouteMouseButton(0, true, rowPoint.X, rowPoint.Y) && Probe::Capture(screen),
          "application captures the actual picker row");
  catalog.ExtraObjects = 128;
  Require(Probe::Add(screen).Activate(), "queue picker rebuild during capture");
  screen.Update(0);
  Require(Rows(picker).front().get() == activeRow && Rows(picker).size() == 1,
          "captured row survives the queued picker rebuild");
  Require(app.RouteMouseButton(0, false, rowPoint.X, rowPoint.Y) && !Probe::Capture(screen) &&
              Rows(picker).front().get() == activeRow && !Probe::PickerVisible(screen),
          "picker hides after selection and retires backing after gesture routing");
  screen.Update(0);
  const auto &objects = world->GetWorldGenSets().VegetationSections.at(Section).Entries;
  Require(Rows(picker).empty() && objects.size() == 2 && objects.back().ObjectName == ObjectId,
          "only the captured row is added, once, to its published section");

  Click(app, Probe::Add(screen), "open larger object picker");
  screen.Update(0);
  Require(Rows(picker).size() == 129 && picker.MaxScrollY() > 0,
          "large picker publishes a scrollable grid");
  Require(picker.OnScroll({0, -1}) && picker.GetScrollY() > 0,
          "picker wheel scroll reaches later rows");
  const int previousScroll = picker.GetScrollY();
  const auto *scrolledRow = Rows(picker).front().get();
  Require(Probe::Add(screen).Activate(), "prepare a replacement for the scrolled picker");
  {
    observation::Scope refuse(0);
    screen.Update(0);
    Require(observation::State.refusals && picker.GetScrollY() == previousScroll &&
                Rows(picker).front().get() == scrolledRow && Probe::PickerVisible(screen),
            "refused replacement keeps the existing picker scroll position");
  }
  ApplicationOperationSourceProbe::CloseWorldGen(app);
  screen.Update(0);
  Require(Rows(picker).empty(), "closing the overlay cancels a pending picker request");

  Require(app.RouteKey(GLFW_KEY_G, GLFW_PRESS, 0), "worldgen key reopens the overlay");
  screen.Update(0);
  auto terrainTab = Point(Probe::Tabs(screen));
  terrainTab.X += Probe::Tabs(screen).GetBounds().W / 3;
  Require(app.RouteMouseButton(0, true, terrainTab.X, terrainTab.Y), "capture terrain tab");
  screen.Update(0);
  Require(Probe::Tabs(screen).GetActiveTab() == 0, "tab highlight and content await release together");
  Require(app.RouteMouseButton(0, false, terrainTab.X, terrainTab.Y), "release terrain tab");
  screen.Update(0);
  auto &content = Probe::Content(screen);
  Require(Probe::Tabs(screen).GetActiveTab() == 1 && Rows(content).size() == 5,
          "terrain tab and five rows publish together");
  Click(app, *Rows(content).front(), "select terrain surface row");
  screen.Update(0);
  const auto &preview = Probe::Preview(screen);
  const auto change = std::find_if(preview.GetChildren().begin(), preview.GetChildren().end(), [](const auto &child)
  {
    const auto *button = dynamic_cast<const UGuiButton *>(child.get());
    return button && button->GetLabel() == "Change..." && button->IsVisible();
  });
  Require(change != preview.GetChildren().end(), "selected terrain row exposes its change action");
  Click(app, **change, "open terrain picker from the preview action");
  screen.Update(0);
  Require(Probe::PickerVisible(screen) && Probe::Title(screen).GetText() == "Pick block for surface" &&
              Rows(picker).size() == 1,
          "terrain preview opens the block picker for the selected slot");

  const auto terrainPick = Point(*Rows(picker).front());
  Require(app.RouteMouseButton(0, true, terrainPick.X, terrainPick.Y) && Probe::Capture(screen),
          "capture the terrain row before leaving the overlay");
  ApplicationOperationSourceProbe::CloseWorldGen(app);
  screen.Update(0);
  Require(app.RouteKey(GLFW_KEY_G, GLFW_PRESS, 0), "reopen before the old pointer release");
  app.RouteMouseButton(0, false, terrainPick.X, terrainPick.Y);
  screen.Update(0);
  Require(world->GetWorldGenSets().Terrain.empty() && !Probe::Capture(screen) && Rows(picker).empty(),
          "release after close never writes through a retired picker row");

  Click(app, *Rows(content).front(), "reselect the terrain surface row");
  screen.Update(0);
  Click(app, **change, "reopen the terrain picker after cancellation");
  screen.Update(0);
  Click(app, *Rows(picker).front(), "apply the captured terrain block");
  screen.Update(0);
  Require(world->GetWorldGenSets().Terrain.at("surface").Block == BlockId && Rows(picker).empty(),
          "the live terrain row updates its named slot");
  const int oreTabX = terrainTab.X + Probe::Tabs(screen).GetBounds().W / 3;
  Require(app.RouteMouseButton(0, true, oreTabX, terrainTab.Y) &&
              app.RouteMouseButton(0, false, oreTabX, terrainTab.Y), "select ores tab");
  screen.Update(0);
  Require(Probe::Tabs(screen).GetActiveTab() == 2 && Rows(content).size() == 2,
          "ore view publishes both built-in slots");
  Click(app, *Rows(content).front(), "select coal ore row");
  screen.Update(0);
  Require(static_cast<const UGuiButton &>(**change).GetLabel() == "Toggle enabled",
          "selected ore exposes its toggle action");
  Click(app, **change, "toggle the selected ore slot");
  screen.Update(0);
  Require(!world->GetWorldGenSets().Ores.at("ore_coal").Enabled &&
              world->GetWorldGenSets().Ores.count("ore_iron") == 0,
          "ore action only toggles the selected entry");
  return {{"status", "PASS"}, {"prefixes", prefixes}, {"captured_row_once", true},
          {"cancelled_release", true}, {"refusal_keeps_scroll", true},
          {"terrain_slot_binding", true}, {"ore_slot_binding", true}};
}
} // namespace worldgen_palette_storage_fixture
} // namespace cutum
