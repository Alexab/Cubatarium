#pragma once
#include "Blocks/Input/BlockInputController.h"
#include "Blocks/Input/PlayerInteractionRouter.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Influence/InfluenceApplier.h"
#include "Creatures/Influence/InfluenceResolver.h"
#include "Items/ItemDefinitionStorage.h"
#include "World/Core/World.h"
#include <chrono>
#include <thread>

namespace cutum
{
template <class Check> void RunItemUseInputQa(UWorld &world, Check check)
{
  auto *actor = world.GetControlledCreature();
  auto &inv = actor->GetInventory();
  InventoryEntryRef apple;
  apple.empty = false;
  apple.kind = InventoryEntryKind::Item;
  apple.Id = "apple";
  apple.count = 3;
  inv.Insert(apple);
  inv.SetActiveSlot(0, 0);
  actor->GetVitals().health = actor->GetVitals().maxHealth;
  actor->GetVitals().satiety = 20;
  UiSettings ui;
  BlockInputContext ctx;
  // The synchronous fixture borrows the world; it never owns its lifetime.
  ctx.World = std::shared_ptr<UWorld>(&world, [](UWorld *) {});
  ctx.Ui = &ui;
  UBlockInputController input;
  auto press = [&] { input.OnMouseButton(MouseButton::Right, true, {}, ctx); };
  auto release = [&]
  { input.OnMouseButton(MouseButton::Right, false, {}, ctx); };
  press();
  std::this_thread::sleep_for(std::chrono::milliseconds(25));
  input.Tick(0.f, ctx);
  check("held food projects actual controller duration without consumption",
        actor->ActiveItemUsePhase() > .001f &&
            actor->ActiveItemUsePhase() < 1.f &&
            inv.Count(InventoryEntryKind::Item, "apple") == 3);
  release();
  check("early food release clears presentation without consumption",
        actor->ActiveItemUsePhase() == 0 &&
            inv.Count(InventoryEntryKind::Item, "apple") == 3);
  press();
  inv.SetActiveSlot(0, 1);
  input.Tick(0.f, ctx);
  inv.SetActiveSlot(0, 0);
  check("returning to a canceled food slot cannot revive held state",
        actor->ActiveItemUsePhase() == 0);
  press();
  input.CancelPointerInteraction(ctx);
  check("pointer cancellation clears held state and preserves food",
        actor->ActiveItemUsePhase() == 0 &&
            inv.Count(InventoryEntryKind::Item, "apple") == 3);
  press();
  actor->CarryHandCount = 1;
  input.Tick(0.f, ctx);
  actor->CarryHandCount = 0;
  check("taking cargo cancels food presentation",
        actor->ActiveItemUsePhase() == 0);
  press();
  world.SetGameMode(WorldGameMode::Creative);
  input.Tick(0.f, ctx);
  world.SetGameMode(WorldGameMode::Survival);
  check("mode round trip cannot revive held food state",
        actor->ActiveItemUsePhase() == 0 &&
            inv.Count(InventoryEntryKind::Item, "apple") == 3);
  press();
  const auto *definition = world.GetItemDefinitionStorage()->Get("apple");
  std::this_thread::sleep_for(
      std::chrono::duration<float>(definition->Use.Duration + .025f));
  input.Tick(0.f, ctx);
  check("fully held food is ready without premature consumption",
        actor->ActiveItemUsePhase() == 1.f &&
            inv.Count(InventoryEntryKind::Item, "apple") == 3);
  release();
  actor->AdvanceInfluenceCooldown(10.f);
  auto use =
      InfluenceResolver::Resolve(world, *actor, world.GetGameMode(), nullptr);
  InfluenceApplier::Apply(world, use, world.GetGameMode(), 0.f);
  actor->ClearIntent();
  check("completed input consumes exactly one food and clears presentation",
        actor->ActiveItemUsePhase() == 0 &&
            inv.Count(InventoryEntryKind::Item, "apple") == 2 &&
            actor->GetVitals().satiety > 20);
  inv = UCreatureInventory{};
  inv.SetItemDefinitions(world.GetItemDefinitionStorage());
  inv.SetCreative(false);
}
} // namespace cutum
