#pragma once

#include "Blocks/BlockDefinitionStorage.h"
#include "Game/GameSession.h"
#include "Gui/Preview/ContentPreviewDock.h"
#include "Gui/Preview/ContentPreviewRenderer.h"
#include "Gui/Widgets/GuiButton.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/GuiScrollView.h"
#include "Gui/Widgets/GuiSlot.h"
#include "Gui/Widgets/GuiTabBar.h"
#include "Render/Textures/TextureCube.h"
#include "ResourcePacks/BlockMergeRegistry.h"
#include "Test/ApplicationOperationStorageFixture.h"
#include "Test/GuiContextStorageFixture.h"
#include "Test/OrdinaryNewObservation.h"
#include "World/Core/World.h"
#include "World/Objects/ObjectLibrary.h"

namespace cutum
{
struct WorldGenPaletteStorageProbe
{
  static UGuiTabBar &Tabs(UWorldGenPaletteScreen &screen) { return *screen.MainTabs; }
  static UGuiTabBar &Sections(UWorldGenPaletteScreen &screen) { return *screen.SectionTabs; }
  static UGuiScrollView &Content(UWorldGenPaletteScreen &screen) { return *screen.ContentScroll; }
  static UGuiScrollView &Picker(UWorldGenPaletteScreen &screen) { return *screen.PickerScroll; }
  static UGuiButton &Primary(UWorldGenPaletteScreen &screen) { return *screen.PrimaryButton; }
  static bool PickerVisible(const UWorldGenPaletteScreen &screen) { return screen.PickerPanel->IsVisible(); }
  static const UGuiLabel &Title(const UWorldGenPaletteScreen &screen) { return *screen.PickerTitleLabel; }
  static const UGuiPanel &Preview(const UWorldGenPaletteScreen &screen) { return *screen.PreviewDock->GetPanel(); }
  static GLuint PreviewTexture(const UWorldGenPaletteScreen &screen) { return screen.PreviewDock->PreviewTexture; }
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
        Require(Probe::Primary(*screen).Activate(), "object picker has an add action");
        screen->Update(0);
        catalog.Replacement = true;
        Require(Probe::Primary(*screen).Activate(), "queue replacement of visible object picker");
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
  Click(app, Probe::Primary(screen), "open initial object picker");
  screen.Update(0);
  auto &picker = Probe::Picker(screen);
  const auto *activeRow = Rows(picker).front().get();
  const auto rowPoint = Point(*activeRow);
  Require(app.RouteMouseButton(0, true, rowPoint.X, rowPoint.Y) && Probe::Capture(screen),
          "application captures the actual picker row");
  catalog.ExtraObjects = 128;
  Require(Probe::Primary(screen).Activate(), "queue picker rebuild during capture");
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

  Click(app, Probe::Primary(screen), "open larger object picker");
  screen.Update(0);
  Require(Rows(picker).size() == 129 && picker.MaxScrollY() > 0,
          "large picker publishes a scrollable grid");
  Require(picker.OnScroll({0, -1}) && picker.GetScrollY() > 0,
          "picker wheel scroll reaches later rows");
  const int previousScroll = picker.GetScrollY();
  const auto *scrolledRow = Rows(picker).front().get();
  Require(Probe::Primary(screen).Activate(), "prepare a replacement for the scrolled picker");
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

inline Json RunOwnerCensus()
{
  using AppProbe = ApplicationOperationSourceProbe;
  auto world = std::make_shared<UWorld>(std::make_shared<UTextureCubeStorage>(nullptr), nullptr);
  WorldGenObjectEntry entry;
  entry.ObjectName = ObjectId;
  world->GetWorldGenSets().VegetationSections[Section].Entries.push_back(std::move(entry));
  UApplication app(nullptr, world, nullptr, nullptr, nullptr, nullptr, nullptr);
  auto &context = app.GetGui();
  Require(context.Initialize(std::make_shared<UShaderManager>(), {}), "initialize owner census GUI");
  AppProbe::SetGameState(app);
  auto &catalog = app.GetGameSession().GetContentCatalog();
  UBlockDefinitionStorage definitions;
  BlockDefinition block;
  block.Id = 234;
  block.Name = BlockId;
  definitions.ReplaceAll({{block.Id, block}}, {{block.Name, block.Id}});
  catalog.IndexBlocks(definitions);

  const auto absent = app.InspectWorldGenPaletteStorage();
  Require(absent.supported && absent.bindingsMatch && !absent.present && !absent.totalBytes,
          "application has no charge before building the WorldGen owner");
  const auto inspect = [&]()
  {
    const size_t attempts = observation::State.attempts;
    const auto value = app.InspectWorldGenPaletteStorage();
    Require(value.supported && value.present && value.bindingsMatch &&
                value.totalBytes == observation::State.live &&
                value.totalBytes == value.screenBytes + value.widgetBytes + value.childrenBytes +
                    value.tabBytes + value.textBytes + value.callbackBytes + value.previewBytes +
                    value.grid.contentBytes + value.grid.pickerBytes &&
                observation::State.attempts == attempts,
            "application census equals independent live requests, without allocation or overlap");
    return value;
  };

  WorldGenPaletteStorage cold, opened, hidden;
  size_t buildRequests = 0;
  {
    observation::Scope observed;
    auto &screen = AppProbe::BuildOwnedWorldGen(app);
    buildRequests = observation::State.attempts;
    cold = inspect();
    Require(buildRequests && !cold.visible && cold.widgets == 20 && !cold.grid.contentBytes &&
                !cold.grid.pickerBytes && cold.previewBytes && !cold.previewTexturePresent,
            "cold WorldGen census includes one hosted preview and two empty grids");
    Require(app.RouteKey(GLFW_KEY_G, GLFW_PRESS, 0), "open application-owned palette");
    screen.Update(0);
    const auto objects = inspect();
    Require(objects.visible && objects.widgets == 21 && objects.grid.sectionTabBytes &&
                objects.grid.contentBytes && objects.totalBytes > cold.totalBytes,
            "open WorldGen census counts the retained object row and section backing");

    auto terrain = Point(Probe::Tabs(screen));
    terrain.X += Probe::Tabs(screen).GetBounds().W / 3;
    Require(app.RouteMouseButton(0, true, terrain.X, terrain.Y) &&
                app.RouteMouseButton(0, false, terrain.X, terrain.Y), "switch owner fixture to terrain");
    screen.Update(0);
    Click(app, *Rows(Probe::Content(screen)).front(), "select surface in owner fixture");
    screen.Update(0);
    const auto &preview = Probe::Preview(screen);
    auto &change = *preview.GetChildren().back();
    Require(change.IsVisible(), "terrain Change action is installed in the hosted preview");
    Click(app, change, "open terrain picker in owner fixture");
    screen.Update(0);
    opened = inspect();
    Require(opened.visible && opened.widgets == 26 && opened.grid.pickerBytes &&
                opened.grid.pickerTitleBytes && opened.previewBytes > cold.previewBytes &&
                Rows(Probe::Picker(screen)).front()->GetBounds().W > 0,
            "application census includes preview metadata and the published resource picker");

    AppProbe::CloseWorldGen(app);
    screen.Update(0);
    hidden = inspect();
    Require(!hidden.visible && !hidden.grid.pickerBytes && hidden.widgets == 25 &&
                hidden.totalBytes < opened.totalBytes,
            "closed owner retires picker rows but reports its actual retained storage");
    AppProbe::RetireWorldGen(app);
    Require(!observation::State.live && !observation::State.blocks,
            "destroying the application overlay retires the complete measured subtree");
  }

  for (size_t cutoff = 0; cutoff <= buildRequests; ++cutoff)
  {
    auto &prior = AppProbe::BuildOwnedWorldGen(app);
    Require(app.RouteKey(GLFW_KEY_G, GLFW_PRESS, 0), "open overlay before replacement");
    prior.Update(0);
    const auto *priorRoot = prior.GetRoot();
    const auto press = Point(Probe::Primary(prior));
    Require(app.RouteMouseButton(0, true, press.X, press.Y) && Probe::Capture(prior),
            "application captures the currently published button");
    observation::Scope observed(cutoff);
    UWorldGenPaletteScreen *replacement = nullptr;
    try { replacement = &AppProbe::BuildOwnedWorldGen(app); }
    catch (const std::bad_alloc &) {}
    observation::State.fail_after = SIZE_MAX;
    if (cutoff < buildRequests)
    {
      const auto active = app.InspectWorldGenPaletteStorage();
      Require(!replacement && observation::State.refusals && !observation::State.live &&
                  active.supported && active.visible && prior.GetRoot() == priorRoot && Probe::Capture(prior),
              "failed owner construction preserves the previous visible root and capture");
      replacement = &AppProbe::BuildOwnedWorldGen(app);
    }
    Require(replacement && replacement->GetRoot() != priorRoot && !Probe::Capture(*replacement) &&
                !inspect().visible, "replacement publishes one closed owner and clears the old capture");
    app.RouteMouseButton(0, false, press.X, press.Y);
    replacement->Update(0);
    Require(!Probe::PickerVisible(*replacement), "release after replacement does not invoke the old action");
    AppProbe::RetireWorldGen(app);
    Require(!observation::State.live && !observation::State.blocks,
            "replacement storage retires on every tested path");
  }

  auto &screen = AppProbe::BuildOwnedWorldGen(app);
  Catalog foreign;
  Require(!screen.InspectOwnedStorage(context, world.get(), &foreign, nullptr, nullptr).supported,
          "unknown borrowed catalog rejects the owner join");
  bool invoked = false;
  Probe::Primary(screen).SetOnClick([&] { invoked = true; });
  Require(!app.InspectWorldGenPaletteStorage().supported && !invoked,
          "unknown live callback is rejected without invocation");
  AppProbe::RetireWorldGen(app);
  return {{"status", "PASS"}, {"cold_bytes", cold.totalBytes}, {"cold_widgets", cold.widgets},
          {"opened_bytes", opened.totalBytes}, {"opened_widgets", opened.widgets},
          {"hidden_bytes", hidden.totalBytes}, {"preview_bytes", opened.previewBytes},
          {"replacement_refusals", buildRequests}, {"no_allocation_census", true},
          {"replacement_retires_capture", true}, {"foreign_catalog_rejected", true},
          {"unknown_action_rejected", true}, {"retired", true}};
}

inline Json RunCompactLayout()
{
  auto bases = std::make_shared<UTextureBaseStorage>();
  bases->RegisterPixels("palette_compact", {1, 1, {46, 123, 197, 255}});
  auto textures = std::make_shared<UTextureCubeStorage>(bases);
  auto blocks = std::make_shared<UBlockDefinitionStorage>();
  std::unordered_map<cutum::BlockId, BlockDefinition> byId;
  std::unordered_map<std::string, cutum::BlockId> byName;
  std::vector<MergedCubeDesc> cubes;
  for (const std::string &name : std::array<std::string, 3>{"water", BlockId, "ore_iron"})
  {
    BlockDefinition block;
    block.Id = static_cast<cutum::BlockId>(40 + cubes.size());
    block.Name = name;
    byId.emplace(block.Id, block);
    byName.emplace(name, block.Id);
    MergedCubeDesc cube;
    cube.Id = block.Id;
    cube.Name = name;
    cube.Stems.fill("palette_compact");
    cubes.push_back(std::move(cube));
  }
  blocks->ReplaceAll(std::move(byId), std::move(byName));
  textures->BuildFromDescriptors(cubes);
  const auto sourceTexture = textures->GetTextures().at(cubes.front().Id).GetTexture();
  Require(sourceTexture && glIsTexture(sourceTexture), "real source texture for compact preview");
  auto shaders = std::make_shared<UShaderManager>();
  UContentPreviewRenderer renderer(std::make_shared<UObjectLibrary>(), textures, blocks, shaders);
  Require(renderer.Initialize(), "initialize actual compact fixture preview renderer");

  auto world = std::make_shared<UWorld>(textures, nullptr);
  Catalog catalog;
  UApplication app(nullptr, world, nullptr, nullptr, nullptr, nullptr, nullptr);
  Require(world->GetWorldFolderPath().empty() && app.GetGui().Initialize(shaders, {}),
          "compact fixture uses an isolated model and initialized GUI");
  ApplicationOperationSourceProbe::SetGameState(app);
  auto &screen = ApplicationOperationSourceProbe::OpenWorldGen(app, catalog, &renderer);
  screen.OnViewportChanged(1280, 768);
  screen.Update(0);
  auto &primary = Probe::Primary(screen);
  const auto &dock = Probe::Preview(screen);
  const auto &wideAction = *dock.GetChildren().back();
  auto &content = Probe::Content(screen);
  auto &picker = Probe::Picker(screen);
  const auto selectTab = [&](int tab)
  {
    auto event = Point(Probe::Tabs(screen));
    event.X += tab * (Probe::Tabs(screen).GetBounds().W / 3);
    Require(app.RouteMouseButton(0, true, event.X, event.Y) &&
                app.RouteMouseButton(0, false, event.X, event.Y), "route responsive fixture tab");
    screen.Update(0);
  };

  selectTab(1);
  Require(!primary.IsVisible() && Rows(content).size() == 5, "wide terrain starts with no main action");
  Click(app, *Rows(content)[3], "select water terrain row");
  screen.Update(0);
  const GLuint wideTexture = Probe::PreviewTexture(screen);
  Require(dock.IsVisible() && wideAction.IsVisible() && !primary.IsVisible() &&
              wideTexture && glIsTexture(wideTexture), "wide terrain produces a live owned preview texture");
  const auto widePress = Point(wideAction);
  Require(app.RouteMouseButton(0, true, widePress.X, widePress.Y) && Probe::Capture(screen),
          "capture wide terrain action before collapse");
  {
    observation::Scope noAllocation(0);
    screen.OnViewportChanged(640, 480);
    screen.RenderPreview();
    Require(!observation::State.attempts && !dock.IsVisible() && primary.IsVisible() &&
                primary.GetLabel() == "Change block" && Probe::PreviewTexture(screen) == wideTexture,
            "collapse publishes the compact action without rendering a replacement preview");
  }
  Require(app.RouteMouseButton(0, false, widePress.X, widePress.Y), "release the old wide action");
  screen.Update(0);
  Require(!Probe::PickerVisible(screen) && !Probe::PreviewTexture(screen) && !glIsTexture(wideTexture) &&
              glIsTexture(sourceTexture), "collapse cancels stale release and retires only the owned preview");

  Click(app, primary, "open compact water picker");
  screen.Update(0);
  Require(Probe::PickerVisible(screen) && Probe::Title(screen).GetText() == "Pick block for water" &&
              Rows(picker).size() == 1, "compact terrain action opens the published water picker");
  const auto *const pickerRow = Rows(picker).front().get();
  screen.OnViewportChanged(1280, 600);
  screen.Update(0);
  Require(!dock.IsVisible() && primary.IsVisible() && Probe::PickerVisible(screen) &&
              Rows(picker).front().get() == pickerRow && Probe::Title(screen).GetText() == "Pick block for water",
          "short viewport preserves the open picker, row and selected terrain slot");
  Click(app, *pickerRow, "apply block in a short viewport");
  screen.Update(0);
  Require(world->GetWorldGenSets().Terrain.size() == 1 &&
              world->GetWorldGenSets().Terrain.at("water").Block == BlockId && !Probe::PickerVisible(screen),
          "short viewport changes exactly the selected terrain slot");

  screen.OnViewportChanged(1100, 700);
  screen.Update(0);
  const GLuint restoredTexture = Probe::PreviewTexture(screen);
  Require(dock.IsVisible() && wideAction.IsVisible() && !primary.IsVisible() &&
              restoredTexture && glIsTexture(restoredTexture), "exact wide breakpoint restores the current preview and action");
  screen.OnViewportChanged(1099, 700);
  screen.Update(0);
  Require(!dock.IsVisible() && primary.IsVisible() && !glIsTexture(restoredTexture),
          "width just below the breakpoint restores compact controls and retires preview");

  selectTab(2);
  Require(!primary.IsVisible() && Rows(content).size() == 2, "changing tabs clears the prior action");
  Click(app, *Rows(content)[1], "select iron ore in the compact pane");
  screen.Update(0);
  Require(primary.IsVisible() && primary.GetLabel() == "Toggle enabled", "compact ore exposes the selected toggle");
  const auto compactPress = Point(primary);
  Require(app.RouteMouseButton(0, true, compactPress.X, compactPress.Y) && Probe::Capture(screen),
          "capture the compact ore action before expansion");
  screen.OnViewportChanged(1280, 768);
  Require(app.RouteMouseButton(0, false, compactPress.X, compactPress.Y), "release the old compact action");
  screen.Update(0);
  Require(dock.IsVisible() && wideAction.IsVisible() && !primary.IsVisible() && world->GetWorldGenSets().Ores.empty(),
          "expanded viewport restores the wide action and cancels the stale compact release");
  screen.OnViewportChanged(1280, 699);
  screen.Update(0);
  Click(app, primary, "toggle iron ore just below the height breakpoint");
  screen.Update(0);
  Require(!dock.IsVisible() && world->GetWorldGenSets().Ores.size() == 1 &&
              !world->GetWorldGenSets().Ores.at("ore_iron").Enabled,
          "compact ore action changes exactly the selected ore once");

  ApplicationOperationSourceProbe::RetireWorldGen(app);
  Require(glIsTexture(sourceTexture) && glGetError() == GL_NO_ERROR, "preview operations preserve the source texture and GL state");
  textures->Clear();
  return {{"status", "PASS"}, {"viewport_cases", {{640, 480}, {1280, 600}, {1100, 700}, {1099, 700}, {1280, 699}}},
          {"terrain_water", true}, {"ore_iron", true}, {"open_picker_resize", true},
          {"stale_releases_cancelled", 2}, {"collapse_allocate0", true},
          {"preview_retired_and_restored", true}, {"borrowed_texture_preserved", true}};
}
} // namespace worldgen_palette_storage_fixture
} // namespace cutum
