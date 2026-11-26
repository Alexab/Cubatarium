#pragma once

#include "Blocks/BlockDefinitionStorage.h"
#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Gui/Interfaces/IUGuiIconSource.h"
#include "Gui/Widgets/GuiButton.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/GuiSlot.h"
#include "Items/ItemDefinitionStorage.h"
#include "Render/Textures/TextureCube.h"
#include "Test/ApplicationOperationStorageFixture.h"
#include "Test/GuiContextStorageFixture.h"
#include "Test/OrdinaryNewObservation.h"
#include "World/Core/World.h"
#include "World/Math/GridMath.h"

namespace cutum
{
struct AnvilStorageProbe
{
  static UGuiButton &Repair(UAnvilScreen &screen) { return *screen.RepairBtn; }
  static const std::string &Status(const UAnvilScreen &screen) { return screen.Status->GetText(); }
  static const std::string &Description(const UAnvilScreen &screen) { return screen.ItemLabel->GetText(); }
  static bool Pending(const UAnvilScreen &screen) { return screen.PendingRepair.has_value(); }
  static GLuint Icon(const UAnvilScreen &screen) { return screen.ItemSlot->GetIconTexture(); }
};

namespace anvil_storage_fixture
{
using gui_context_fixture::Require;
using Json = nlohmann::json;
using Probe = AnvilStorageProbe;

inline Json Snapshot(const UCreatureInventory &inventory)
{
  Json out;
  inventory.SerializeToJson(out);
  return out;
}

inline void Populate(UCreatureInventory &inventory, UItemDefinitionStorage &items,
                     size_t recoveryCount, bool withMaterial)
{
  inventory.SetItemDefinitions(&items);
  Json slots = Json::array({{{"id", "iron_pickaxe"}, {"kind", "item"}, {"wear", 0.8f}},
                           {{"id", withMaterial ? "iron_ingot" : ""}, {"kind", "item"}, {"count", 3}},
                           {{"id", "iron_pickaxe"}, {"kind", "item"}, {"wear", 0.5f}},
                           {{"id", std::string(45, 'b')}, {"kind", "block"}}});
  Json recovery = Json::array();
  for (size_t i = 0; i < recoveryCount; ++i)
    recovery.push_back({{"id", std::string(53, 'r') + std::to_string(i)}, {"kind", "item"}});
  inventory.DeserializeFromJson({{"inventory_version", 3}, {"owned_slots", std::move(slots)},
                                 {"inventory_recovery", std::move(recovery)}});
}

inline Json CheckRepairTransaction(UItemDefinitionStorage &items)
{
  UCreatureInventory absent;
  Populate(absent, items, 80, false);
  const auto before = Snapshot(absent);
  std::string reason;
  Require(!absent.RepairActive(items, reason) && reason == "missing_repair_material" &&
              Snapshot(absent) == before, "missing repair material preserves full inventory");

  const auto check = [&](size_t recoveryRows)
  {
    for (size_t cutoff = 0; ; ++cutoff)
    {
      auto inventory = std::make_unique<UCreatureInventory>();
      Populate(*inventory, items, recoveryRows, true);
      const auto previous = Snapshot(*inventory);
      const auto identity = inventory->GetActiveEntryRef()->instanceId;
      const auto *const recovery = inventory->GetRecovery().data();
      std::string result;
      observation::Scope epoch(cutoff);
      bool refused = false;
      try { Require(inventory->RepairActive(items, result), "valid survival repair succeeds"); }
      catch (const std::bad_alloc &) { refused = true; }
      observation::State.fail_after = SIZE_MAX;
      if (refused)
      {
        Require(!observation::State.live && Snapshot(*inventory) == previous,
                "refused repair preserves active and inactive inventory backing");
        Require(inventory->RepairActive(items, result), "repair succeeds when preparation can complete");
      }
      const auto peak = observation::State.peak;
      Require(inventory->GetRecovery().data() == recovery && inventory->GetRecovery().size() == recoveryRows &&
                  inventory->GetActiveEntryRef()->instanceId == identity &&
                  inventory->GetActiveEntryRef()->wear == 0.8f - items.Get("iron_pickaxe")->Repair.Amount &&
                  inventory->Count(InventoryEntryKind::Item, "iron_ingot") == 2,
              "repair publishes exact wear and one material while retaining identity");
      inventory.reset();
      Require(!observation::State.live && !observation::State.blocks, "repair trial storage retires");
      if (!refused)
        return std::pair(cutoff, peak);
    }
  };
  const auto populated = check(80);
  Require(check(0) == populated && populated.first > 0,
          "repair requests and peak are independent of recovery size");
  return {{"refusals", populated.first}, {"peak", populated.second}, {"recovery_independent", true}};
}

class TestIcons final : public IUGuiIconSource
{
public:
  bool Refuse{false};
  GLuint GetItemStateIcon(const InventoryEntryRef &) override
  {
    if (Refuse)
      throw std::bad_alloc{};
    return 71;
  }
  GLuint GetItemIconTexture(const std::string &) override { return 0; }
  GLuint GetBlockIconTexture(const std::string &) override { return 0; }
  GLuint GetBlockIconTextureIfCached(const std::string &) const override { return 0; }
  GLuint GetObjectIconTexture(const std::string &) override { return 0; }
  GLuint GetObjectIconTextureIfCached(const std::string &) const override { return 0; }
  GLuint GetCreatureIconTexture(const std::string &) override { return 0; }
  GLuint GetSkinIconTexture(const std::string &) override { return 0; }
  void QueueBlockIcon(const std::string &) override {}
  void WarmupBlockIcons(size_t) override {}
  void WarmupItemIcons(size_t) override {}
  size_t PendingBlockIcons() const override { return 0; }
  size_t PendingItemIcons() const override { return 0; }
};

inline size_t CheckPublication(UGuiContext &context, UWorld &world, TestIcons &icons)
{
  size_t refusals = 0;
  for (size_t cutoff = 0; ; ++cutoff)
  {
    auto screen = std::make_unique<UAnvilScreen>(&world, &icons);
    screen->OnAttach(context);
    screen->Build(context);
    screen->SetVisible(true);
    const auto *const previous = screen->GetRoot();
    const auto description = Probe::Description(*screen);
    observation::Scope epoch(cutoff);
    bool refused = false;
    try { screen->Build(context); }
    catch (const std::bad_alloc &) { refused = true; }
    observation::State.fail_after = SIZE_MAX;
    if (refused)
    {
      ++refusals;
      Require(screen->GetRoot() == previous && Probe::Description(*screen) == description &&
                  Probe::Icon(*screen) == 71 && !observation::State.live,
              "refused anvil rebuild retains the visible root and live description");
      screen->Build(context);
    }
    const auto before = observation::State.attempts;
    observation::State.fail_after = before;
    const auto storage = screen->InspectOwnedStorage(context, &world, &icons);
    screen->Update(0);
    Require(storage.supported && storage.widgets == 6 && !storage.callbackBytes &&
                storage.requested - storage.screenBytes == observation::State.live &&
                description == Probe::Description(*screen) && Probe::Icon(*screen) == 71 &&
                before == observation::State.attempts && screen->GetRoot() != previous,
            "anvil rebuild census matches observation and idle update allocates nothing");
    screen.reset();
    Require(!observation::State.live && !observation::State.blocks, "all new anvil storage retires");
    if (!refused)
      return refusals;
  }
}

inline Json Run()
{
  UItemDefinitionStorage items;
  items.Load("content/items");
  const auto transaction = CheckRepairTransaction(items);
  auto definitions = std::make_shared<UCreatureDefinitionStorage>();
  Require(definitions->LoadFile("models/creatures/human/creature.json"), "load anvil fixture actor");
  auto textures = std::make_shared<UTextureCubeStorage>(nullptr);
  auto blocks = std::make_shared<UBlockDefinitionStorage>();
  constexpr BlockId stationId = 37;
  BlockDefinition station;
  station.Id = stationId;
  station.Name = "anvil_very_damaged";
  blocks->ReplaceAll({{stationId, station}}, {{station.Name, stationId}});
  auto world = std::make_shared<UWorld>(textures, nullptr);
  world->SetBlockDefinitionStorage(blocks);
  world->SetCreatureDefinitionStorage(definitions);
  world->SetItemDefinitionStorage(&items);
  world->SetGameMode(WorldGameMode::Survival);
  world->AddUser("anvil_fixture");
  Require(world->SetCurrentUserName("anvil_fixture"), "bind the anvil fixture user");
  auto *actor = world->GetControlledCreature();
  Require(actor && world->GetBlockRegistry().GetTypeNameById(stationId) == station.Name,
          "controlled creature and damaged station are attached to the real world");
  auto &inventory = actor->GetInventory();
  Populate(inventory, items, 80, true);

  TestIcons icons;
  UApplication app(nullptr, world, nullptr, nullptr, nullptr, nullptr, nullptr);
  Require(app.GetGui().Initialize(std::make_shared<UShaderManager>(), {}), "initialize anvil application GUI");
  ApplicationOperationSourceProbe::SetGameState(app);
  const auto rebuilds = CheckPublication(app.GetGui(), *world, icons);
  auto &screen = ApplicationOperationSourceProbe::BuildAnvil(app, &icons);
  screen.OnViewportChanged(1024, 768);
  app.OpenAnvilScreen();
  const auto previous = Snapshot(inventory);
  Require(Probe::Repair(screen).Activate() && Probe::Status(screen) == "Requires nearby anvil" &&
              Snapshot(inventory) == previous, "absent station leaves inventory untouched");
  world->GetBlockWorld().SetBlock(WorldPosToBlock(actor->GetBodyOrigin()) + glm::ivec3(1, 0, 0), stationId);

  {
    observation::Scope epoch(0);
    Require(Probe::Repair(screen).Activate() && Probe::Pending(screen) && observation::State.refusals,
            "preparation refusal retains the accepted repair target");
  }
  Require(Snapshot(inventory) == previous, "refused action leaves live material and tool intact");
  inventory.SetActiveSlot(0, 2);
  const auto switched = Snapshot(inventory);
  screen.Update(0);
  Require(!Probe::Pending(screen) && Probe::Status(screen) == "repair_source_changed" &&
              Snapshot(inventory) == switched, "pending repair never applies to a different tool");
  inventory.SetActiveSlot(0, 0);
  screen.Update(0);
  const auto description = Probe::Description(screen);
  const auto identity = inventory.GetActiveEntryRef()->instanceId;
  const auto bounds = Probe::Repair(screen).GetBounds();
  const int x = bounds.X + 5, y = bounds.Y + 5;
  Require(app.RouteMouseButton(0, true, x, y), "application routes the real repair button press");
  {
    observation::Scope epoch(0);
    Require(app.RouteMouseButton(0, false, x, y) && Probe::Pending(screen) &&
                observation::State.refusals,
            "application release admits one repair and retains it after preparation refusal");
  }
  Require(Snapshot(inventory) == previous, "captured repair leaves the original inventory until it can commit");
  icons.Refuse = true;
  screen.Update(0);
  Require(!Probe::Pending(screen) && Probe::Status(screen) == "Repaired" &&
              Probe::Description(screen) == description &&
              inventory.Count(InventoryEntryKind::Item, "iron_ingot") == 2,
          "refused icon refresh preserves the displayed item and retires the committed operation");
  icons.Refuse = false;
  screen.Update(0);
  screen.Update(0);
  Require(Probe::Description(screen) != description && inventory.GetActiveEntryRef()->instanceId == identity &&
              inventory.Count(InventoryEntryKind::Item, "iron_ingot") == 2,
          "redraw resumes after refusal without a second repair");

  const auto repaired = Snapshot(inventory);
  {
    observation::Scope epoch(0);
    Require(Probe::Repair(screen).Activate() && Probe::Pending(screen),
            "close fixture has one pending repair");
  }
  Require(app.RouteMouseButton(0, true, x, y) && app.RouteKey(GLFW_KEY_ESCAPE, GLFW_PRESS, 0),
          "the normal close route cancels a captured repair button");
  app.OpenAnvilScreen();
  app.RouteMouseButton(0, false, x, y);
  screen.Update(0);
  Require(!Probe::Pending(screen) && Snapshot(inventory) == repaired,
          "release after reopening does not run a retired button action");
  screen.SetWorld(nullptr);
  Require(!Probe::Icon(screen), "world replacement retires the borrowed icon before a refresh");
  screen.Update(0);
  Require(Probe::Description(screen) == "No world" && !Probe::Icon(screen),
          "world removal clears the previous icon and description");
  Require(rebuilds > 0, "real anvil replacement exercises allocation refusal");
  return {{"status", "PASS"}, {"transaction", transaction}, {"rebuild_refusals", rebuilds},
          {"idle_allocate0", true}, {"stale_selection_cancelled", true},
          {"published_action_once", true}, {"closed_capture_cancelled", true}};
}
} // namespace anvil_storage_fixture
} // namespace cutum
