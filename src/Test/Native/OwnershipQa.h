#pragma once
#include "Blocks/Input/PlayerInteractionRouter.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include "Creatures/Influence/InfluenceApplier.h"
#include "Creatures/Influence/InfluenceResolver.h"
#include "Game/GameSession.h"
#include "Game/Survival/SurvivalSystems.h"
#include "World/Core/World.h"
#include "World/Items/WorldItemSystem.h"
#include "World/Math/GridMath.h"
#include <fstream>
namespace cutum
{
inline void CheckOwnershipDeathAndRecovery(UWorld &world,
                                           nlohmann::json &report)
{
  auto *actor = world.GetControlledCreature();
  auto camera = world.GetCurrentUserCamera();
  auto &inv = actor->GetInventory();
  auto &physics = UWorldItemSystem::Instance();
  const auto feet = actor->GetBodyOrigin();
  auto check = [&](const std::string &name, bool ok)
  { report["checks"].push_back({{"name", name}, {"passed", ok}}); };
  auto tick = [&](int n)
  {
    for (int i = 0; i < n; ++i)
      physics.Tick(world, 1.f / 60);
  };
  auto entry = [](const char *id)
  {
    InventoryEntryRef e;
    e.empty = false;
    e.kind = InventoryEntryKind::Item;
    e.Id = id;
    e.count = 1;
    return e;
  };
  physics.Clear();
  world.SetGameMode(WorldGameMode::Creative);
  inv = UCreatureInventory{};
  inv.SetItemDefinitions(world.GetItemDefinitionStorage());
  inv.SetCreative(true);
  world.SetThrowSettings({});
  inv.Insert(entry("wood_bow"));
  inv.SetActiveSlot(0, 0);
  const auto victim = world.SpawnCreature("sheep", feet + glm::vec3(0, 0, 4),
                                          "", CreatureSpawnSource::Qa);
  bool fired = false;
  if (auto *target = world.GetCreature(victim))
  {
    target->GetVitals().health = .001f;
    target->GetVitals().fatalWounds = target->GetVitals().maxFatalWounds - 1;
    target->GetAttributes().luck = 0;
    fired =
        physics.FireArrow(world, *actor, camera->GetPosition(),
                          glm::normalize(target->GetCollisionVolume().center -
                                         camera->GetPosition()),
                          1);
  }
  world.SetGameMode(WorldGameMode::Survival);
  tick(120);
  nlohmann::json result;
  physics.Serialize(result);
  bool contextualLoot = true;
  const auto *def = world.GetCreatureDefinition("sheep");
  for (const auto &drop : def->drops)
  {
    bool found = false;
    for (const auto &row : result["world_physical_v2"])
      if (row["item"] == drop.itemId && row["domain"] == "creative")
        found = true;
    contextualLoot = contextualLoot && found;
  }
  check("projectile death loot keeps release context across mode change",
        fired && !world.GetCreature(victim) && !def->drops.empty() &&
            contextualLoot);
  const auto count = physics.Count();
  InfluenceApplier::ApplyImpact(world, victim, actor->GetId(), 1000, feet);
  check("death processing issues loot once", physics.Count() == count);
  if (world.GetCreature(victim))
    world.RemoveCreature(victim);
  physics.Clear();
  world.SetGameMode(WorldGameMode::Creative);
  inv = UCreatureInventory{};
  inv.SetItemDefinitions(world.GetItemDefinitionStorage());
  inv.SetCreative(true);
  inv.Insert(entry("iron_pickaxe"));
  const auto original = inv.GetSlot(0);
  physics.SpawnStack(feet + glm::vec3(0, 1, 2), original,
                     InventoryDomain::Creative);
  physics.ReconcileOwnership(world);
  physics.Serialize(result);
  check("ambiguous duplicate world ownership is quarantined",
        physics.Count() == 0 &&
            inv.GetSlot(0).instanceId == original.instanceId &&
            result["physical_recovery"].size() == 1);
  InventoryEntryRef removed;
  inv.Take(0, 1, removed);
  physics.Deserialize(result);
  physics.ReconcileOwnership(world);
  check("quarantine cannot resurrect a subsequently spent instance",
        physics.Count() == 0 && inv.GetSlot(0).empty);
  auto malformed = result;
  malformed["physical_recovery"] = nlohmann::json::array();
  malformed["world_physical_v2"].push_back({{"item", "iron_ingot"},
                                            {"kind", 4},
                                            {"count", 3},
                                            {"position", "invalid"}});
  physics.Deserialize(malformed);
  physics.Serialize(result);
  check("malformed physical property retained in recovery",
        result["physical_recovery"].size() == 1 &&
            result["physical_recovery"][0]["count"] == 3);
  physics.Clear();
  inv = UCreatureInventory{};
  inv.SetItemDefinitions(world.GetItemDefinitionStorage());
  inv.SetCreative(true);
  InventoryEntryRef unavailable;
  unavailable.empty = false;
  unavailable.kind = InventoryEntryKind::Block;
  unavailable.Id = "missing_pack_block";
  unavailable.count = 3;
  inv.Insert(unavailable);
  physics.ReconcileOwnership(world);
  check("missing block definitions move into mode-local inventory recovery",
        inv.GetSlot(0).empty && inv.GetRecovery().size() == 1 &&
            inv.GetRecovery()[0].count == 3);
  world.SetGameMode(WorldGameMode::Survival);
  check("unavailable creative items stay out of survival recovery",
        inv.GetRecovery().empty());
  physics.Clear();
}
inline void RunOwnershipQa(UWorld &world, UGameSession &session,
                           const std::string &path)
{
  auto *actor = world.GetControlledCreature();
  auto camera = world.GetCurrentUserCamera();
  if (!actor || !camera)
    throw std::runtime_error("Ownership QA needs actor and camera");
  auto &physics = UWorldItemSystem::Instance();
  auto &inv = actor->GetInventory();
  auto oldInv = inv;
  const auto oldMode = world.GetGameMode();
  const auto oldRules = world.GetThrowSettings();
  const auto feet = actor->GetBodyOrigin();
  const auto oldEye = camera->GetPosition();
  const auto yaw = camera->GetYaw(), pitch = camera->GetPitch();
  const auto floor = WorldPosToBlock(feet) - glm::ivec3(0, 1, 0);
  const auto stone = world.GetBlockRegistry().GetIdByTypeName("stone");
  for (int x = -4; x <= 4; ++x)
    for (int z = -4; z <= 9; ++z)
      for (int y = 0; y <= 6; ++y)
        world.GetBlockWorld().SetBlock(floor + glm::ivec3(x, y, z),
                                       y == 0 || z == 8 ? stone : BLOCK_AIR);
  nlohmann::json report = {{"checks", nlohmann::json::array()}};
  auto check = [&](const std::string &name, bool ok)
  { report["checks"].push_back({{"name", name}, {"passed", ok}}); };
  auto tick = [&](int frames)
  {
    for (int i = 0; i < frames; ++i)
      physics.Tick(world, 1.f / 60);
  };
  auto entry = [](const std::string &id, bool block = false)
  {
    InventoryEntryRef e;
    e.empty = false;
    e.Id = id;
    e.kind = block ? InventoryEntryKind::Block : InventoryEntryKind::Item;
    e.count = 1;
    return e;
  };
  auto reset = [&](WorldGameMode mode)
  {
    physics.Clear();
    world.SetGameMode(mode);
    inv = UCreatureInventory{};
    inv.SetItemDefinitions(world.GetItemDefinitionStorage());
    inv.SetCreative(mode == WorldGameMode::Creative);
    actor->CarryHandCount = 0;
    actor->ThrowCharge = 0;
    actor->GetVitals().fatigue = 0;
    world.SetPlayerDead(false);
    actor->SetBodyOrigin(feet);
    camera->SetPosition(feet + actor->GetEyeOffset());
    camera->SetOrientation(90.f, 0);
    world.SetThrowSettings({});
    world.SyncPhysicalCreatureIndex();
  };
  auto finite = [&](const std::string &id)
  {
    int n = 0;
    for (size_t i = 0; i < 40; ++i)
    {
      const auto &e = inv.GetSlot(i);
      if (!e.empty && !e.IsCatalogReference() && e.Id == id)
        n += e.count;
    }
    return n;
  };
  auto bodies = [&]()
  {
    nlohmann::json j;
    physics.Serialize(j);
    return j["world_physical_v2"];
  };
  std::string reason;
  // Creative instant mining, distinct reference and finite stock.
  reset(WorldGameMode::Creative);
  auto reference = entry("wood", true);
  reference.form = InventoryEntryForm::CatalogReference;
  inv.AssignToHotbar(3, 9, reference);
  const auto node = floor + glm::ivec3(0, 1, 1);
  world.ReplaceBlockAt(node, "wood");
  PlayerInteractionRouter::BeginDigIntent(*actor, node);
  auto prediction =
      InfluenceResolver::Resolve(world, *actor, world.GetGameMode(), nullptr);
  InfluenceApplier::Apply(world, prediction, world.GetGameMode(), 1.f / 60);
  actor->ClearIntent();
  tick(100);
  check(
      "creative instant mining produces finite pickup beside catalog reference",
      world.GetBlockWorld().GetBlock(node) == BLOCK_AIR &&
          finite("wood") == 1 && inv.GetSlot(39).IsCatalogReference());
  auto survivalBefore = inv;
  world.SetGameMode(WorldGameMode::Survival);
  check("mode change preserves independent survival inventory",
        finite("wood") == 0);
  world.SetGameMode(WorldGameMode::Creative);
  check("creative mined stock survives mode roundtrip", finite("wood") == 1);
  inv.AssignToHotbar(3, 8,
                     []
                     {
                       InventoryEntryRef e;
                       e.empty = false;
                       e.kind = InventoryEntryKind::Item;
                       e.Id = "bucket";
                       e.count = 1;
                       e.form = InventoryEntryForm::CatalogReference;
                       return e;
                     }());
  inv.SetActiveSlot(3, 8);
  const auto waterNode = floor + glm::ivec3(1, 1, 1);
  world.ReplaceBlockAt(waterNode, "water");
  const bool filled = USurvivalSystems::Instance().Use(
      world, *actor, camera->GetPosition(),
      glm::normalize(glm::vec3(waterNode) - camera->GetPosition()), reason);
  check("stateful catalog bucket becomes one finite filled instance",
        filled && !inv.GetSlot(38).IsCatalogReference() &&
            inv.GetSlot(38).contentsId == "water" &&
            inv.GetSlot(38).contentsUnits == 1);
  world.ReplaceBlockAt(waterNode, "air");
  // World property domain survives loading and never converts on mode switch.
  physics.Clear();
  auto resource = entry("iron_ingot");
  physics.SpawnStack(feet + glm::vec3(0, .15f, 1), resource,
                     InventoryDomain::Creative);
  nlohmann::json domainSave;
  physics.Serialize(domainSave);
  auto legacyDomainSave = domainSave;
  legacyDomainSave["world_physical_v1"] = legacyDomainSave["world_physical_v2"];
  legacyDomainSave.erase("world_physical_v2");
  for (auto &row : legacyDomainSave["world_physical_v1"])
  {
    row.erase("domain");
    row["creative_copy"] = true;
  }
  physics.Deserialize(legacyDomainSave);
  nlohmann::json migratedPhysical;
  physics.Serialize(migratedPhysical);
  check("legacy creative copies migrate to v2 domains without deletion",
        !migratedPhysical.contains("world_physical_v1") &&
            migratedPhysical["world_physical_v2"][0]["domain"] == "creative");
  world.SetGameMode(WorldGameMode::Survival);
  tick(90);
  check("survival cannot pick a creative world drop after load",
        finite("iron_ingot") == 0 && physics.Count() == 1);
  world.SetGameMode(WorldGameMode::Creative);
  tick(90);
  check("creative picks a finite world drop",
        finite("iron_ingot") == 1 && physics.Count() == 0);
  // Fresh physical transfer must not instantly return to the thrower.
  for (auto mode : {WorldGameMode::Survival, WorldGameMode::Creative})
  {
    reset(mode);
    auto tool = entry("iron_pickaxe");
    tool.wear = .62f;
    inv.Insert(tool);
    inv.SetActiveSlot(0, 0);
    const auto identity = inv.GetSlot(0).instanceId;
    session.DiscardInventory();
    tick(100);
    check(
        std::string(mode == WorldGameMode::Creative ? "creative" : "survival") +
            " drop remains visible without immediate repickup",
        inv.GetSlot(0).empty && physics.Count() == 1);
    actor->SetBodyOrigin(feet + glm::vec3(4, 0, 0));
    tick(2);
    actor->SetBodyOrigin(feet);
    tick(90);
    check("drop pickup retains finite identity and wear",
          inv.GetSlot(0).instanceId == identity &&
              inv.GetSlot(0).wear == .62f && physics.Count() == 0);
  }
  // Restrictions apply before extraction and again after grabbing from the
  // world.
  reset(WorldGameMode::Creative);
  inv.Insert(entry("iron_sword"));
  inv.SetActiveSlot(0, 0);
  const auto swordIdentity = inv.GetSlot(0).instanceId;
  ThrowSettings restricted;
  restricted.preset = ThrowPreset::Restricted;
  world.SetThrowSettings(restricted);
  check("restricted throw refuses sword before extraction",
        !physics.BeginThrow(world, *actor, reason) &&
            inv.GetSlot(0).instanceId == swordIdentity &&
            !physics.HasCargo(actor->GetId()));
  world.SetThrowSettings({});
  check("free policy starts sword windup",
        physics.BeginThrow(world, *actor, reason));
  world.SetThrowSettings(restricted);
  check("release rechecks policy before the next physics step",
        !physics.Release(world, *actor, {0, 0, 1}) &&
            physics.HasCargo(actor->GetId()));
  tick(1);
  check("policy change cancels windup but retains held item",
        actor->ThrowCharge == 0 && physics.HasCargo(actor->GetId()));
  check("held item cannot bypass restricted policy",
        !physics.BeginThrow(world, *actor, reason) &&
            physics.HasCargo(actor->GetId()));
  check("restricted item can be gently placed",
        physics.Release(world, *actor, glm::vec3(0, 0, 1), true));
  // Ordinary payloads retain state for hits and misses in both modes.
  for (auto mode : {WorldGameMode::Survival, WorldGameMode::Creative})
    for (const auto *id : {"iron_sword", "iron_shovel", "apple", "iron_ingot",
                           "stone", "bucket"})
      for (bool hitTarget : {false, true})
      {
        reset(mode);
        auto payload = entry(id, std::string(id) == "stone");
        if (std::string(id) == "iron_sword" || std::string(id) == "iron_shovel")
          payload.wear = .47f;
        if (std::string(id) == "bucket")
        {
          payload.contentsId = "water";
          payload.contentsUnits = 1;
        }
        inv.Insert(payload);
        inv.SetActiveSlot(0, 0);
        const auto source = inv.GetSlot(0);
        uint64_t target = 0;
        float health = 0;
        glm::vec3 aim(0, .1f, 1);
        if (hitTarget)
        {
          const auto before = world.GetGameMode();
          world.SetGameMode(WorldGameMode::Creative);
          target = world.SpawnCreature(
              "wasp", camera->GetPosition() + glm::vec3(0, 0, 3), "",
              CreatureSpawnSource::Qa);
          world.SetGameMode(before);
          if (auto *c = world.GetCreature(target))
          {
            health = c->GetVitals().health;
            c->GetVitals().fatalWounds = c->GetVitals().maxFatalWounds - 1;
          }
        }
        bool armed = physics.BeginThrow(world, *actor, reason);
        tick(1);
        if (auto *c = world.GetCreature(target))
        {
          const auto rows = bodies();
          if (!rows.empty())
          {
            const auto &p = rows[0]["position"];
            glm::vec3 origin(p[0].get<float>(), p[1].get<float>(),
                             p[2].get<float>());
            aim = glm::normalize(c->GetCollisionVolume().center - origin);
          }
        }
        physics.SetThrowCharge(*actor, 1);
        bool released = physics.Release(world, *actor, aim);
        tick(180);
        bool retained = false;
        for (const auto &r : bodies())
          if (r["instance"].get<uint64_t>() == source.instanceId)
            retained = r["wear"].get<float>() == source.wear &&
                       r["units"].get<int>() == source.contentsUnits;
        // A near landing can already have been collected; either location must
        // retain the same instance.
        for (size_t i = 0; i < 40; ++i)
          if (inv.GetSlot(i).instanceId == source.instanceId &&
              !inv.GetSlot(i).empty)
            retained = inv.GetSlot(i).wear == source.wear &&
                       inv.GetSlot(i).contentsUnits == source.contentsUnits;
        check(std::string(mode == WorldGameMode::Creative ? "creative "
                                                          : "survival ") +
                  id +
                  (hitTarget ? " hit preserves payload"
                             : " miss preserves payload"),
              armed && released && retained &&
                  (!hitTarget ||
                   (target != 0 &&
                    (!world.GetCreature(target) ||
                     world.GetCreature(target)->GetVitals().health < health))));
        if (target)
          world.RemoveCreature(target);
        if (std::string(id) == "stone")
        {
          auto ref = entry("stone", true);
          ref.form = InventoryEntryForm::CatalogReference;
          if (mode == WorldGameMode::Creative)
            inv.AssignToHotbar(3, 9, ref);
          inv.Insert(entry("stone", true));
          for (const auto &body : physics.GetItems())
            if (body.stack.instanceId == source.instanceId)
            {
              actor->SetBodyOrigin(body.position + glm::vec3(4, 0, 0));
              tick(1);
              actor->SetBodyOrigin(
                  {body.position.x + 1.f, feet.y, body.position.z});
              tick(100);
              break;
            }
          const auto snapshot = session.GetCharacterStatsSnapshot();
          int displayed = 0;
          for (const auto &slot : snapshot.slots)
            if (!slot.empty && !slot.IsCatalogReference() && slot.Id == id)
              displayed += slot.count;
          check(std::string(mode == WorldGameMode::Creative ? "creative "
                                                            : "survival ") +
                    (hitTarget ? "hit block pickup increments backpack"
                               : "missed block pickup increments backpack"),
                finite("stone") == 2 && displayed == 2 &&
                    (mode != WorldGameMode::Creative ||
                     inv.GetSlot(39).IsCatalogReference()));
        }
      }
  reset(WorldGameMode::Creative);
  auto blockRef = entry("stone", true);
  blockRef.form = InventoryEntryForm::CatalogReference;
  inv.AssignToHotbar(0, 0, blockRef);
  auto blockStack = entry("stone", true);
  blockStack.count = 63;
  inv.Insert(blockStack);
  for (int i = 0; i < 38; ++i)
    inv.Insert(entry("iron_pickaxe"));
  blockStack.count = 3;
  physics.SpawnStack(feet + glm::vec3(1, .5f, 0), blockStack,
                     InventoryDomain::Creative);
  tick(100);
  int remainingBlocks = 0;
  for (const auto &body : physics.GetItems())
    if (body.stack.Id == "stone")
      remainingBlocks += body.stack.count;
  check("creative partial block pickup fills finite stack beside catalog",
        finite("stone") == 64 && remainingBlocks == 2 &&
            session.GetBarSlots(0)[1].CountLabel == "64" &&
            session.GetBarSlots(0)[0].CountLabel.empty());
  InventoryEntryRef removed;
  inv.Take(1, 64, removed);
  tick(30);
  check("creative remaining blocks enter freed backpack slot",
        finite("stone") == 2 && physics.Count() == 0 &&
            session.GetBarSlots(0)[1].CountLabel == "2");
  reset(WorldGameMode::Creative);
  for (int n = 0; n < 1000; ++n)
    physics.Spawn(feet + glm::vec3(3, 3, 5), "arrow", 1, false,
                  InventoryDomain::Creative);
  tick(2);
  nlohmann::json crowded;
  physics.Serialize(crowded);
  physics.Deserialize(crowded);
  int retainedUnits = 0;
  for (const auto &body : physics.GetItems())
    retainedUnits += body.stack.count;
  check("active budget defers and persists every extra drop",
        physics.GetActiveItems().size() <= 512 && retainedUnits == 1000);
  reset(WorldGameMode::Creative);
  auto mob = world.SpawnCreature("rat", feet + glm::vec3(0, 0, 1.5f), "",
                                 CreatureSpawnSource::Qa);
  if (auto *c = world.GetCreature(mob))
  {
    auto direction =
        glm::normalize(c->GetCollisionVolume().center - camera->GetPosition());
    bool grabbed =
        physics.Grab(world, *actor, camera->GetPosition(), direction, reason);
    const auto fatigue = actor->GetVitals().fatigue;
    tick(30);
    check("creative creature hold preserves identity without fatigue",
          grabbed && c->ExternalMotion && c->GetId() == mob &&
              actor->GetVitals().fatigue == fatigue);
    tick(360);
    check("creative creatures still resist and regain motion",
          !physics.HasCargo(actor->GetId()) && !c->ExternalMotion);
    world.RemoveCreature(mob);
  }
  else
    check("creative creature fixture spawned", false);
  CheckOwnershipDeathAndRecovery(world, report);
  physics.Clear();
  world.SetGameMode(oldMode);
  inv = oldInv;
  world.SetThrowSettings(oldRules);
  actor->SetBodyOrigin(feet);
  camera->SetPosition(oldEye);
  camera->SetOrientation(yaw, pitch);
  actor->CarryHandCount = 0;
  actor->ThrowCharge = 0;
  report["passed"] = true;
  for (const auto &c : report["checks"])
    if (!c["passed"].get<bool>())
      report["passed"] = false;
  std::ofstream out(path);
  out << report.dump(2) << '\n';
}
} // namespace cutum
