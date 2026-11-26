#pragma once

#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Gui/Widgets/GuiButton.h"
#include "Gui/Widgets/GuiScrollView.h"
#include "Gui/Widgets/GuiTextInput.h"
#include "Test/ApplicationOperationStorageFixture.h"
#include "Test/GuiContextStorageFixture.h"
#include "Test/OrdinaryNewObservation.h"
#include "World/Core/World.h"

namespace cutum
{
struct CraftingStorageProbe
{
  static bool Loaded(const UCraftingScreen &screen) { return screen.RecipesLoaded; }
  static UGuiScrollView &Scroll(UCraftingScreen &screen) { return *screen.Scroll; }
  static const std::string &Query(const UCraftingScreen &screen) { return screen.Search->GetText(); }
  static void SetQuery(UCraftingScreen &screen, const std::string &query)
  {
    screen.Search->SetText(query);
    screen.RowsDirty = true;
  }
  static const RecipeDefinition *Pending(const UCraftingScreen &screen) { return screen.PendingRecipe; }
  static bool Held(const UCraftingScreen &screen) { return screen.PointerPressed; }
};

namespace crafting_storage_fixture
{
using gui_context_fixture::Require;
using Json = nlohmann::json;
using Probe = CraftingStorageProbe;

inline Json InventoryState(const UCreatureInventory &inv)
{
  Json out;
  inv.SerializeToJson(out);
  return out;
}

inline Json CheckBatch()
{
  const std::string sourceId(45, 's'), firstAlternative(43, 'a'), secondAlternative(46, 'b');
  const std::string resultId(48, 'r');
  RecipeDefinition recipe;
  recipe.Id = "fixture_recipe";
  recipe.Station = "fixture_station";
  recipe.Inputs = {{sourceId, 2, {}}, {sourceId, 1, {}},
                   {"alternative", 2, {firstAlternative, secondAlternative}}};
  recipe.Output = {resultId, 2, {}};
  URecipeRegistry recipes;
  const auto make = [&](size_t recoveryCount)
  {
    auto inv = std::make_unique<UCreatureInventory>();
    const auto row = [](const std::string &id, int count)
    { return Json{{"id", id}, {"kind", "block"}, {"count", count}}; };
    Json recovery = Json::array();
    for (size_t i = 0; i < recoveryCount; ++i)
      recovery.push_back(row(std::string(63, 'q') + std::to_string(i), 1));
    inv->DeserializeFromJson({{"inventory_version", 3},
        {"owned_slots", Json::array({row(sourceId, 20), row(firstAlternative, 1),
                                     row(secondAlternative, 6)})},
        {"inventory_recovery", std::move(recovery)}});
    return inv;
  };
  auto negative = make(80);
  const auto negativeBefore = InventoryState(*negative);
  std::string reason;
  Require(!recipes.TryCraftBatch(*negative, recipe, 3, false, &reason) &&
              reason == "Requires nearby fixture_station" && InventoryState(*negative) == negativeBefore,
          "required station leaves the live inventory intact");
  Require(!recipes.TryCraftBatch(*negative, recipe, 4, true, &reason) &&
              reason == "Missing alternative" && InventoryState(*negative) == negativeBefore,
          "failure after a partial batch leaves every live slot and identity intact");

  const auto preview = [&](size_t recoveryCount)
  {
    const auto inv = make(recoveryCount);
    const auto previous = InventoryState(*inv);
    observation::Counts sample;
    {
      observation::Scope epoch;
      Require(recipes.CanCraftBatch(*inv, recipe, 3, true), "batch preview uses the actual rules");
      sample = observation::Snapshot();
      Require(!sample.live && !sample.blocks, "batch preview retains no trial backing");
    }
    Require(InventoryState(*inv) == previous, "batch preview leaves the source unchanged");
    return sample;
  };
  const auto empty = preview(0), populated = preview(80);
  Require(empty.peak == populated.peak && empty.attempts == populated.attempts,
          "unrelated recovery rows do not enlarge the batch trial");

  size_t refusals{}, peak{};
  for (size_t cutoff = 0; ; ++cutoff)
  {
    auto inv = make(80);
    const auto expected = InventoryState(*inv);
    const auto *const recovery = inv->GetRecovery().data();
    observation::Scope epoch(cutoff);
    bool refused = false;
    try { Require(recipes.TryCraftBatch(*inv, recipe, 3, true), "valid batch succeeds"); }
    catch (const std::bad_alloc &) { refused = true; }
    observation::State.fail_after = SIZE_MAX;
    if (refused)
    {
      ++refusals;
      Require(!observation::State.live && InventoryState(*inv) == expected,
              "refused batch retires the trial and preserves the full input");
      Require(recipes.TryCraftBatch(*inv, recipe, 3, true), "identical batch succeeds on retry");
    }
    peak = observation::State.peak;
    Require(inv->GetRecovery().data() == recovery && inv->GetRecovery().size() == 80 &&
                inv->Count(InventoryEntryKind::Block, sourceId) == 11 &&
                inv->Count(InventoryEntryKind::Block, firstAlternative) == 0 &&
                inv->Count(InventoryEntryKind::Block, secondAlternative) == 1 &&
                inv->Count(InventoryEntryKind::Block, resultId) == 6,
            "batch commits exact duplicate costs, ordered alternatives and all outputs once");
    inv.reset();
    Require(!observation::State.live && !observation::State.blocks, "completed batch backing retires");
    if (!refused)
      break;
  }
  return {{"refusals", refusals}, {"peak", peak}, {"preview_peak", populated.peak},
          {"preview_attempts", populated.attempts}, {"recovery_independent", true}};
}

inline Json CheckRows()
{
  UGuiContext context;
  Require(context.Initialize(std::make_shared<UShaderManager>(), {}), "crafting context initializes");
  const auto make = [&]
  {
    auto screen = std::make_unique<UCraftingScreen>(nullptr, nullptr, nullptr);
    screen->OnAttach(context);
    return screen;
  };
  size_t shellRefusals{};
  for (size_t cutoff = 0; ; ++cutoff)
  {
    auto screen = make();
    observation::Scope epoch(cutoff);
    bool refused = false;
    try { screen->Build(context); }
    catch (const std::bad_alloc &) { refused = true; }
    observation::State.fail_after = SIZE_MAX;
    if (refused)
    {
      ++shellRefusals;
      Require(!screen->GetRoot() && !observation::State.live, "cold shell refuses before publication");
      screen->Build(context);
    }
    Require(!Probe::Loaded(*screen) && !screen->GetRoot()->IsVisible(),
            "shell binds while recipe parsing remains lazy");
    screen.reset();
    Require(!observation::State.live && !observation::State.blocks, "all shell storage retires");
    if (!refused)
      break;
  }

  size_t rowRefusals{}, bytes{};
  for (size_t cutoff = 0; ; ++cutoff)
  {
    auto screen = make();
    screen->Build(context);
    screen->OnViewportChanged(1024, 768);
    screen->SetVisible(true);
    auto &scroll = Probe::Scroll(*screen);
    scroll.SetScrollY(300);
    const auto &rows = scroll.Content().GetChildren();
    Require(Probe::Loaded(*screen) && rows.size() > 10 && scroll.GetScrollY() > 0,
            "open crafting uses actual definitions and scrollable live rows");
    const auto *const oldRow = rows.front().get();
    const size_t oldCount = rows.size();
    Probe::SetQuery(*screen, "wOoD pIcKaXe");
    observation::Scope epoch(cutoff);
    screen->Update(0);
    const bool refused = observation::State.refusals > 0;
    observation::State.fail_after = SIZE_MAX;
    if (refused)
    {
      ++rowRefusals;
      Require(rows.size() == oldCount && rows.front().get() == oldRow &&
                  !observation::State.live && scroll.GetScrollY() > 0,
              "filtered row refusal preserves the prior publication and scroll state");
      screen->Update(0);
    }
    const auto before = observation::State.attempts;
    observation::State.fail_after = before;
    const auto census = screen->InspectRowsStorage();
    Require(census.supported && census.rows == 1 && census.callbackBytes == 0 &&
                census.requested == observation::State.live && before == observation::State.attempts &&
                scroll.GetScrollY() == 0 && rows.front()->GetBounds().Y >= scroll.GetBounds().Y,
            "filtered row storage matches observer and scroll is clamped before positioning");
    bytes = census.requested;
    observation::State.fail_after = SIZE_MAX;
    screen.reset();
    Require(!observation::State.live && !observation::State.blocks, "published row backing retires");
    if (!refused)
      break;
  }
  return {{"shell_refusals", shellRefusals}, {"filtered_refusals", rowRefusals},
          {"filtered_row_bytes", bytes}, {"in_place_census_allocate0", true}};
}

inline void CheckApplicationInput()
{
  auto definitions = std::make_shared<UCreatureDefinitionStorage>();
  Require(definitions->LoadFile("models/creatures/human/creature.json"), "load actor definition");
  auto world = std::make_shared<UWorld>(nullptr, nullptr);
  world->SetCreatureDefinitionStorage(definitions);
  world->SetGameMode(WorldGameMode::Creative);
  world->AddUser("crafting_fixture");
  Require(world->SetCurrentUserName("crafting_fixture"), "bind the crafting fixture user");
  auto *const actor = world->GetControlledCreature();
  Require(actor != nullptr, "crafting fixture uses a controlled creature");

  UApplication app(nullptr, world, nullptr, nullptr, nullptr, nullptr, nullptr);
  Require(app.GetGui().Initialize(std::make_shared<UShaderManager>(), {}), "crafting application GUI initializes");
  ApplicationOperationSourceProbe::SetGameState(app);
  auto &screen = ApplicationOperationSourceProbe::BuildCrafting(app);
  screen.OnViewportChanged(1024, 768);
  app.OpenCraftingScreen();
  auto &scroll = Probe::Scroll(screen);
  Probe::SetQuery(screen, "wood pickaxe");
  screen.Update(0);
  const auto &rows = scroll.Content().GetChildren();
  Require(rows.size() == 1, "selected recipe is visible before the input gesture");
  const auto *const oldRow = rows.front().get();
  const auto button = oldRow->GetChildren().back()->GetBounds();
  const int x = button.X + 5, y = button.Y + 5;
  Require(app.RouteMouseButton(0, true, x, y) && Probe::Held(screen),
          "application routes button press and captures the recipe row");
  Probe::SetQuery(screen, "stick");
  screen.Update(0);
  Require(rows.front().get() == oldRow, "filter defers replacement during captured input");
  Require(app.RouteMouseButton(0, false, x, y) && !Probe::Held(screen) &&
              Probe::Pending(screen) && Probe::Pending(screen)->Id == "wood_pickaxe",
          "release activates the same recipe row and retires capture");
  const auto previous = InventoryState(actor->GetInventory());
  {
    observation::Scope epoch(0);
    screen.Update(0);
    Require(observation::State.refusals && Probe::Pending(screen),
            "processing allocation refusal keeps the accepted operation pending");
  }
  Require(InventoryState(actor->GetInventory()) == previous, "refused processing keeps the original inventory");
  screen.Update(0);
  screen.Update(0);
  Require(!Probe::Pending(screen) && Probe::Query(screen) == "stick" &&
              actor->GetInventory().Count(InventoryEntryKind::Block, "wood_pickaxe") == 1 &&
              !rows.empty() && rows.front().get() != oldRow,
          "later frame commits the accepted action once and publishes the deferred filter");

  const auto nextButton = rows.front()->GetChildren().back()->GetBounds();
  const int nextX = nextButton.X + 5, nextY = nextButton.Y + 5;
  Require(app.RouteMouseButton(0, true, nextX, nextY) && Probe::Held(screen) &&
              app.RouteKey(GLFW_KEY_ESCAPE, GLFW_PRESS, 0) && !Probe::Held(screen),
          "closing the overlay cancels its captured row gesture");
  const auto closed = InventoryState(actor->GetInventory());
  app.OpenCraftingScreen();
  app.RouteMouseButton(0, false, nextX, nextY);
  screen.Update(0);
  Require(!Probe::Pending(screen) && InventoryState(actor->GetInventory()) == closed,
          "release after reopening cannot activate a retired recipe button");
}

inline Json Run()
{
  const auto batch = CheckBatch();
  const auto rows = CheckRows();
  CheckApplicationInput();
  return {{"status", "PASS"}, {"batch", batch}, {"rows", rows},
          {"captured_action_once", true}, {"closed_capture_cancelled", true}};
}
} // namespace crafting_storage_fixture
} // namespace cutum
