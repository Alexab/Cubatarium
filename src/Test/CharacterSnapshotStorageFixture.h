#pragma once

#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Creatures/Definition/SkinDefinitionStorage.h"
#include "Creatures/Influence/StatusEffectCatalog.h"
#include "Creatures/Player/User.h"
#include "Game/CharacterSnapshotStorage.h"
#include "Game/GameSession.h"
#include "Items/ItemDefinitionStorage.h"
#include "Test/GuiContextStorageFixture.h"
#include "Test/OrdinaryNewObservation.h"
#include "World/Core/World.h"

namespace cutum::character_snapshot_fixture
{
using gui_context_fixture::Require;
using Json = nlohmann::json;

template <typename Read, typename Verify>
Json Measure(Read &&read, Verify &&verify)
{
  CharacterSnapshotStorage measured;
  size_t attempts{}, peak{};
  for (size_t cutoff = 0; ; ++cutoff)
  {
    observation::Scope epoch(cutoff);
    bool refused = false;
    try
    {
      const auto snap = read();
      const auto before = observation::State.attempts;
      const auto constructionPeak = observation::State.peak;
      measured = InspectCharacterSnapshot(snap);
      Require(measured.heap.supported && measured.heap.requested == observation::State.live &&
                  before == observation::State.attempts,
              "snapshot heap census equals the ordinary allocation observer");
      observation::State.fail_after = SIZE_MAX;
      verify(snap);
      attempts = before;
      peak = constructionPeak;
    }
    catch (const std::bad_alloc &) { refused = true; }
    Require(!observation::State.live && !observation::State.blocks,
            "snapshot construction and refusal retire all measured allocations");
    if (!refused)
      break;
  }
  return {{"attempts", attempts}, {"peak", peak}, {"inline_bytes", measured.inlineBytes},
          {"heap", measured.heap.requested}, {"identity", measured.identityBytes},
          {"equipment", measured.equipmentBytes}, {"slots", measured.slotBytes},
          {"recovery", measured.recoveryBytes}, {"effects", measured.effectBytes},
          {"carried", measured.carriedBytes}, {"protection", measured.protectionBytes}};
}

inline Json ItemRow(const std::string &id, int count, const char *form = "finite")
{
  return {{"id", id}, {"kind", "item"}, {"count", count}, {"form", form}};
}

inline Json Run()
{
  UItemDefinitionStorage items;
  items.Load("content/items");
  auto definitions = std::make_shared<UCreatureDefinitionStorage>();
  auto skins = std::make_shared<USkinDefinitionStorage>();
  Require(definitions->LoadFile("models/creatures/human/creature.json"),
          "load the production human definition for snapshot sources");
  skins->Load("models/skins");
  auto world = std::make_shared<UWorld>(nullptr, nullptr);
  UGameSession session(nullptr, world);
  world->SetCreatureDefinitionStorage(definitions);
  world->SetSkinDefinitionStorage(skins);
  world->SetItemDefinitionStorage(&items);
  world->SetGameMode(WorldGameMode::Survival);
  world->SetDifficulty(WorldDifficulty::Hard);
  Require(!session.GetControlledVitals() && !session.GetCharacterLookSnapshot().valid &&
              !session.GetCharacterPageSnapshot(0, 40).valid &&
              session.GetCharacterRecoveryCount() == 0,
          "session with no controlled creature exposes no visible stats or recovery");

  world->AddUser("snapshot_fixture");
  Require(world->SetCurrentUserName("snapshot_fixture"), "bind the snapshot test user");
  auto *actor = world->GetControlledCreature();
  auto user = world->GetCurrentUser();
  Require(actor && user, "the real session reads the spawned controlled creature");
  user->SetSelectedSkinId("human_adventurer");
  actor->GetVitals().health = 71.f;
  actor->GetVitals().breath = 53.f;
  actor->GetArmorGroups().Ratings = {{"fleshy", 120}, {std::string(50, 'g'), 31}};
  actor->GetStatusEffects().push_back({"slow", 9.f, 0.f, 1});
  actor->GetStatusEffects().push_back({std::string(66, 'e'), 2.f, 0.f, 3});
  UStatusEffectCatalog::Get();

  auto &inventory = actor->GetInventory();
  Json rows = Json::array();
  for (size_t i = 0; i < 40; ++i)
    rows.push_back({{"empty", true}});
  rows[0] = ItemRow("copper_ingot", 2);
  rows[11] = ItemRow("copper_ingot", 3);
  rows[5] = ItemRow("wood_pickaxe", 1, "catalog");
  rows[6] = ItemRow("wood_pickaxe", 1);
  rows[23] = ItemRow("bucket", 1);
  rows[23]["contents_id"] = std::string(35, 'w');
  rows[23]["contents_units"] = 2;
  rows[30] = ItemRow(std::string(40, 'u'), 4);
  rows[30]["kind"] = "object";
  Json recovery = Json::array();
  const auto recoveryId = [](size_t row) { return std::string(48, 'r') + std::to_string(row); };
  for (size_t i = 0; i < 85; ++i)
  {
    auto item = ItemRow(recoveryId(i), int(i + 1));
    item["contents_id"] = std::string(40, 'c');
    item["contents_units"] = 3;
    recovery.push_back(std::move(item));
  }
  inventory.DeserializeFromJson({{"inventory_version", 3}, {"owned_slots", rows},
                                  {"inventory_recovery", recovery}});
  inventory.SetActiveSlot(2, 3);
  const auto gear = [](std::string id)
  {
    InventoryEntryRef result;
    result.Id = std::move(id);
    result.kind = InventoryEntryKind::Item;
    result.empty = false;
    result.count = 1;
    return result;
  };
  Require(inventory.EquipArmor(1, gear("copper_chest"), items, false) &&
              inventory.EquipOffhand(gear("iron_shield"), items, false),
          "equip real armor and shield for protection parity");

  {
    observation::Scope epoch(0);
    const auto vitals = session.GetControlledVitals();
    const auto recoveryCount = session.GetCharacterRecoveryCount();
    const auto attempts = observation::State.attempts;
    observation::State.fail_after = SIZE_MAX;
    Require(vitals && vitals->health == 71.f && vitals->breath == 53.f,
            "HUD vitals match the controlled creature");
    Require(recoveryCount == 85, "recovery count matches normalized inventory");
    Require(!attempts, "vitals and recovery count allocate nothing");
  }
  const auto look = Measure([&] { return session.GetCharacterLookSnapshot(); },
                            [&](const CharacterLookSnapshot &snap)
  {
    Require(snap.valid && snap.OwnerId == actor->GetId() && snap.typeId == "human" &&
                snap.skinId == "human_adventurer" && snap.equippedArmor[1].itemId == "copper_chest" &&
                snap.equippedTools[0].itemId == "bucket" && snap.equippedTools[1].itemId == "iron_shield",
            "preview captures the real effective identity and both hands");
  });
  const auto verifyPage = [&](const CharacterPageSnapshot &snap)
  {
    Require(snap.valid && snap.activeRow == 2 && snap.activeSlot == 3 &&
                snap.vitals.health == 71.f && snap.recoveryCount == 85 && snap.effects.size() == 2 &&
                snap.fleshyProtection == 63 && snap.difficulty == WorldDifficulty::Hard &&
                snap.slots[23].contentsUnits == 2 && snap.slots[23].contentsId.size() == 35 &&
                snap.gameMode == WorldGameMode::Survival,
            "page reads matching status, inventory, recovery count and effective protection");
  };
  const auto main = Measure([&] { return session.GetCharacterPageSnapshot(0, 0); },
                            [&](const CharacterPageSnapshot &snap)
  {
    verifyPage(snap);
    Require(snap.recovery.empty(), "closed recovery has no copied rows");
  });
  const auto recoveryPage = Measure([&] { return session.GetCharacterPageSnapshot(40, SIZE_MAX); },
                                    [&](const CharacterPageSnapshot &snap)
  {
    verifyPage(snap);
    Require(snap.recovery.size() == 40 && snap.recovery.front().Id == recoveryId(40) &&
                snap.recovery.back().Id == recoveryId(79), "bounded middle page keeps the original row order");
  });
  const auto full = Measure([&] { return session.GetCharacterStatsSnapshot(); },
                            [&](const CharacterStatsSnapshot &snap)
  {
    verifyPage(snap);
    Require(snap.recovery.size() == 85 && snap.recovery.back().Id == recoveryId(84) &&
                snap.protection.Get("fleshy") == 63 && snap.protection.Get(std::string(50, 'g')) == 31 &&
                snap.carriedItems.size() == 3 && snap.carriedItems[0].id == "bucket" &&
                snap.carriedItems[0].count == 1 && snap.carriedItems[1].id == "copper_ingot" &&
                snap.carriedItems[1].count == 5 && snap.carriedItems[2].id == "wood_pickaxe" &&
                snap.carriedItems[2].count == 1,
            "full snapshot preserves legacy recovery, protection and sorted carried item sums");
  });

  for (const auto *id : {"", "human_guard", "sheep_wool_black", "unregistered_skin"})
  {
    actor->SetSkinId(id);
    const auto rendered = world->GetResolvedAppearanceShared(*actor);
    Require(rendered && session.GetCharacterLookSnapshot().skinId == rendered->effectiveSkinId,
            "identity query and visual resolution agree for fallback, valid and incompatible skins");
  }
  auto tail = session.GetCharacterPageSnapshot(80, 40);
  Require(tail.recovery.size() == 5 && tail.recovery.back().Id == recoveryId(84) &&
              session.GetCharacterPageSnapshot(SIZE_MAX, SIZE_MAX).recovery.empty(),
          "last recovery page and out-of-range offset are bounded");
  actor->CarryHandCount = 1;
  Require(session.GetCharacterPageSnapshot(0, 0).fleshyProtection == 84,
          "using the main hand gates the offhand contribution");
  inventory.SetCreative(true);
  {
    const auto creative = session.GetCharacterStatsSnapshot();
    Require(creative.slots[5].IsCatalogReference() && creative.carriedItems.size() == 1 &&
                creative.carriedItems.front().id == "wood_pickaxe" &&
                creative.carriedItems.front().count == -1,
            "creative snapshot preserves unlimited catalog reference totals separately");
  }
  inventory.SetCreative(false);
  actor->GetVitals().health = 15.f;
  inventory.DeserializeFromJson(Json{{"inventory_version", 3}});
  Require(tail.recovery[0].Id == recoveryId(80) && tail.vitals.health == 71.f &&
              session.GetCharacterRecoveryCount() == 0,
          "returned data owns its backing across source replacement");
  Require(look["attempts"].get<size_t>() > 0 && main["recovery"] == 0 &&
              full["heap"].get<size_t>() > recoveryPage["heap"].get<size_t>() &&
              recoveryPage["heap"].get<size_t>() > main["heap"].get<size_t>(),
          "observed storage follows the requested display scope");
  return {{"status", "PASS"}, {"vitals_allocate0", true}, {"count_allocate0", true},
          {"effective_skin_parity", true}, {"source_replacement_isolated", true},
          {"look", look}, {"page", main}, {"recovery", recoveryPage}, {"full", full}};
}
} // namespace cutum::character_snapshot_fixture
