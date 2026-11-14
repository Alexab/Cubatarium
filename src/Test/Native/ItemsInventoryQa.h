#include "Blocks/Input/PlayerInteractionRouter.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include "Creatures/Environment/CreatureEnvironment.h"
#include "Creatures/Influence/InfluenceApplier.h"
#include "Creatures/Influence/InfluenceResolver.h"
#include "Creatures/Influence/StatusEffectCatalog.h"
#include "Creatures/Influence/StatusEffectSystem.h"
#include "Test/Native/SurvivalLoopQa.h"
#include "World/Physics/WorldChunkDirtyService.h"
#pragma once
#include "Creatures/Core/Creature.h"
#include "Game/Crafting/RecipeRegistry.h"
#include "Game/Survival/SurvivalSystems.h"
#include "Items/ItemDefinitionStorage.h"
#include "Render/Camera/Camera.h"
#include "World/Core/World.h"
#include "World/Items/WorldItemSystem.h"
#include "World/Math/GridMath.h"
#include <chrono>
#include <fstream>
namespace cutum
{
// Explicit opt-in native fixture. It only runs in a separately named QA world.
inline void RunItemsInventoryQa(UWorld &world, const std::string &path)
{
  if (world.GetWorldName().find("QA") == std::string::npos)
    throw std::runtime_error("Item QA requires a dedicated QA world");
  auto *actor = world.GetControlledCreature();
  auto camera = world.GetCurrentUserCamera();
  if (!actor || !camera)
    return;
  auto &physics = UWorldItemSystem::Instance();
  auto &survival = USurvivalSystems::Instance();
  nlohmann::json report;
  report["checks"] = nlohmann::json::array();
  auto check = [&](std::string name, bool ok)
  { report["checks"].push_back({{"name", name}, {"passed", ok}}); };
  auto entry = [](const std::string &id, int count = 1, float wear = 0.f)
  {
    InventoryEntryRef e;
    e.empty = false;
    e.Id = id;
    e.kind = InventoryEntryKind::Item;
    e.count = count;
    e.wear = wear;
    return e;
  };
  world.SetGameMode(WorldGameMode::Survival);
  world.SetDifficulty(WorldDifficulty::Normal);
  auto &inv = actor->GetInventory();
  inv = UCreatureInventory{};
  inv.SetItemDefinitions(world.GetItemDefinitionStorage());
  inv.SetCreative(false);
  const auto floor =
      WorldPosToBlock(actor->GetBodyOrigin()) + glm::ivec3(0, 6, 0);
  const auto stone = world.GetBlockRegistry().GetIdByTypeName("stone");
  for (int x = -4; x <= 4; ++x)
    for (int z = -4; z <= 12; ++z)
      for (int y = 0; y <= 7; ++y)
        world.GetBlockWorld().SetBlock(floor + glm::ivec3(x, y, z),
                                       y == 0 || z == 10 ? stone : BLOCK_AIR);
  if (auto *dirty = world.GetChunkDirtyService())
    for (int x = -4; x <= 4; ++x)
      for (int z = -4; z <= 12; ++z)
      {
        dirty->MarkCollisionRebuild(world, floor + glm::ivec3(x, 0, z));
        dirty->MarkCollisionRebuild(world, floor + glm::ivec3(x, 7, z));
      }
  const auto feet = glm::vec3(floor) + glm::vec3(0, .501f, 0);
  actor->SetBodyOrigin(feet);
  camera->SetPosition(feet + actor->GetEyeOffset());
  world.InvalidateBlockMesh();
  camera->SetOrientation(90.f, 0.f);
  const auto eye = camera->GetPosition();
  const glm::vec3 forward(0, 0, 1);
  auto spawn = [&](const std::string &type, glm::vec3 position)
  {
    auto mode = world.GetGameMode();
    world.SetGameMode(WorldGameMode::Creative);
    auto id = world.SpawnCreature(type, position, "", CreatureSpawnSource::Qa);
    if (!id)
    {
      auto *def = world.GetCreatureDefinition(type);
      size_t count = 0;
      world.ForEachCreature([&](const UCreature &) { ++count; });
      nlohmann::json diagnostic = {{"type", type},
                                   {"known", def != nullptr},
                                   {"mode", int(world.GetGameMode())},
                                   {"hardcore_dead", world.IsHardcoreDead()},
                                   {"count", count}};
      if (def)
      {
        auto adjusted = AdjustSpawnBodyOrigin(world, *def, position);
        diagnostic["spawnable"] = def->catalog.spawnable;
        diagnostic["role"] = int(def->role);
        diagnostic["position"] = {position.x, position.y, position.z};
        diagnostic["adjusted"] = {adjusted.x, adjusted.y, adjusted.z};
        diagnostic["habitat"] = HabitatAllowsAtForSpawn(
            world, def->habitat, adjusted, def->bounds.restSizeBlocks);
        auto volume =
            CollisionVolumeFromBody(adjusted, def->bounds.restSizeBlocks);
        diagnostic["blocks"] = world.CheckBlockCollisionVolume(volume);
        diagnostic["creatures"] = world.CheckCreatureCollisionVolume(volume, 0);
      }
      report["spawn_diagnostics"].push_back(diagnostic);
    }
    world.SetGameMode(mode);
    if (auto *c = world.GetCreature(id))
      c->GetInventory().SetCreative(mode == WorldGameMode::Creative);
    return id;
  };
  auto tick = [&](int steps)
  {
    for (int i = 0; i < steps; ++i)
      physics.Tick(world, 1.f / 60);
  };
  std::string reason;
  inv.Insert(entry("iron_pickaxe", 1, .72f));
  inv.Insert(entry("iron_pickaxe", 1, .13f));
  inv.SetActiveSlot(0, 0);
  const auto identity = inv.GetSlot(0).instanceId;
  check("take exact worn instance for throw",
        physics.BeginThrow(world, *actor, reason) &&
            inv.Count(InventoryEntryKind::Item, "iron_pickaxe") == 1 &&
            actor->CarryHandCount == 1);
  tick(1);
  actor->ThrowCharge = 1;
  check("release charged item", physics.Release(world, *actor, forward));
  nlohmann::json flying;
  physics.Serialize(flying);
  physics.Deserialize(flying);
  tick(90);
  bool retained = false;
  for (const auto &i : physics.GetItems())
    if (i.stack.instanceId == identity)
      retained = i.stack.wear == .72f && i.stack.contentsUnits == 0;
  check("flight and save retain identity and durability", retained);
  check("wall CCD stops thrown tool",
        physics.Count() > 0 &&
            physics.GetItems().front().position.z < float(floor.z) + 9.6f);
  physics.Clear();
  actor->CarryHandCount = 0;
  actor->ThrowCharge = 0;
  auto small = spawn("rat", feet + glm::vec3(0, 0, 1.7f));
  if (auto *c = world.GetCreature(small))
  {
    const auto center = c->GetCollisionVolume().center;
    const auto direction = glm::normalize(center - eye);
    const auto hp = c->GetVitals().health;
    c->GetStatusEffects().push_back({"slow", 6.f, 0, 1});
    c->GetInventory().SetItemDefinitions(world.GetItemDefinitionStorage());
    c->GetInventory().Insert(entry("apple", 2));
    check("grab a small creature",
          physics.Grab(world, *actor, eye, direction, reason) &&
              c->ExternalMotion && actor->CarryHandCount == 1);
    world.SaveCreatures(path + ".held.creatures.json");
    world.LoadCreatures(path + ".held.creatures.json");
    c = world.GetCreature(small);
    tick(1);
    check("held save retains creature ID and possessions",
          c && c->GetId() == small &&
              c->GetInventory().Count(InventoryEntryKind::Item, "apple") == 2 &&
              c->GetStatusEffects().size() == 1 && c->GetVitals().health == hp);
    actor->ThrowCharge = 1;
    physics.Release(world, *actor, forward);
    world.SaveCreatures(path + ".flight.creatures.json");
    world.LoadCreatures(path + ".flight.creatures.json");
    tick(90);
    c = world.GetCreature(small);
    check("hard impact damages thrown creature",
          !c || c->GetVitals().health < hp);
    check("AI ownership restored after impact", !c || !c->ExternalMotion);
    world.RemoveCreature(small);
  }
  else
    check("small creature fixture spawned", false);
  physics.Clear();
  actor->CarryHandCount = 0;
  actor->ThrowCharge = 0;
  auto missile = spawn("rat", feet + glm::vec3(0, 0, 1.7f));
  auto victim = spawn("sheep", feet + glm::vec3(0, 0, 4.5f));
  if (auto *m = world.GetCreature(missile); m && world.GetCreature(victim))
  {
    auto *v = world.GetCreature(victim);
    const float mh = m->GetVitals().health, vh = v->GetVitals().health;
    bool grabbed = physics.Grab(
        world, *actor, eye,
        glm::normalize(m->GetCollisionVolume().center - eye), reason);
    tick(1);
    auto cargo =
        physics.GetItems().empty() ? eye : physics.GetItems().front().position;
    actor->ThrowCharge = 1;
    bool released = physics.Release(
        world, *actor, glm::normalize(v->GetCollisionVolume().center - cargo));
    tick(60);
    m = world.GetCreature(missile);
    v = world.GetCreature(victim);
    check("creature projectile damages both participants",
          grabbed && released && (!m || m->GetVitals().health < mh) &&
              (!v || v->GetVitals().health < vh));
  }
  else
    check("bilateral impact fixture spawned", false);
  world.RemoveCreature(missile);
  world.RemoveCreature(victim);
  physics.Clear();
  actor->CarryHandCount = 0;
  actor->ThrowCharge = 0;
  auto doomedMissile = spawn("rat", feet + glm::vec3(0, 0, 1.7f));
  auto doomedTarget = spawn("sheep", feet + glm::vec3(0, 0, 4.5f));
  if (auto *m = world.GetCreature(doomedMissile);
      m && world.GetCreature(doomedTarget))
  {
    auto *v = world.GetCreature(doomedTarget);
    m->GetVitals().health = .001f;
    v->GetVitals().health = .001f;
    bool grabbed = physics.Grab(
        world, *actor, eye,
        glm::normalize(m->GetCollisionVolume().center - eye), reason);
    tick(1);
    auto cargo =
        physics.GetItems().empty() ? eye : physics.GetItems().front().position;
    actor->ThrowCharge = 1;
    bool released = physics.Release(
        world, *actor, glm::normalize(v->GetCollisionVolume().center - cargo));
    tick(60);
    m = world.GetCreature(doomedMissile);
    v = world.GetCreature(doomedTarget);
    check("mutually lethal impact handles both creatures",
          grabbed && released && (!m || m->GetVitals().health <= 0) &&
              (!v || v->GetVitals().health <= 0));
    size_t drops = physics.Count();
    tick(60);
    check("resting lethal contact creates no repeated drops",
          physics.Count() <= drops);
  }
  else
    check("mutual death fixtures spawned", false);
  world.RemoveCreature(doomedMissile);
  world.RemoveCreature(doomedTarget);
  physics.Clear();
  actor->CarryHandCount = 0;
  actor->ThrowCharge = 0;
  auto two = spawn("sheep", feet + glm::vec3(0, 0, 1.8f));
  if (auto *c = world.GetCreature(two))
  {
    check("two-hand creature grab",
          physics.Grab(world, *actor, eye,
                       glm::normalize(c->GetCollisionVolume().center - eye),
                       reason) &&
              actor->CarryHandCount == 2);
    const auto hp = c->GetVitals().health;
    camera->SetOrientation(90.f, -70.f);
    tick(8);
    check("looking down keeps a ground-obstructed creature held",
          physics.HasCargo(actor->GetId()) && c->ExternalMotion);
    const float lowY = c->GetCollisionVolume().center.y;
    for (int frame = 1; frame <= 24; ++frame)
    {
      camera->SetOrientation(90.f, -70.f + 145.f * frame / 24.f);
      tick(1);
    }
    check("raising aim lifts a grounded creature without releasing it",
          physics.HasCargo(actor->GetId()) &&
              c->GetCollisionVolume().center.y > lowY + .3f);
    for (int frame = 1; frame <= 24; ++frame)
    {
      camera->SetOrientation(90.f + 180.f * frame / 24.f, 0.f);
      tick(1);
    }
    check("turning aim retains the creature without floor penetration",
          physics.HasCargo(actor->GetId()) &&
              c->GetCollisionVolume().center.y -
                      c->GetCollisionVolume().halfExtents.y >=
                  float(floor.y) + .499f);
    camera->SetOrientation(90.f, 75.f);
    const auto ceiling = floor + glm::ivec3(0, 3, 0);
    world.GetBlockWorld().SetBlock(ceiling, stone);
    tick(8);
    auto volume = c->GetCollisionVolume();
    volume.halfExtents -= glm::vec3(.001f);
    check("ceiling restricts held cargo without dropping or penetrating it",
          physics.HasCargo(actor->GetId()) &&
              !world.CheckBlockCollisionVolume(volume));
    world.GetBlockWorld().SetBlock(ceiling, BLOCK_AIR);
    camera->SetOrientation(90.f, 0.f);
    physics.Release(world, *actor, forward, true);
    tick(45);
    check("gentle placement does no contact damage",
          world.GetCreature(two) &&
              world.GetCreature(two)->GetVitals().health == hp);
    world.RemoveCreature(two);
  }
  else
    check("two-hand creature fixture spawned", false);
  physics.Clear();
  actor->CarryHandCount = 0;
  actor->ThrowCharge = 0;
  auto hostile = spawn("wasp", feet + forward * 1.8f);
  if (auto *c = world.GetCreature(hostile))
  {
    bool grabbed = physics.Grab(
        world, *actor, eye,
        glm::normalize(c->GetCollisionVolume().center - eye), reason);
    check("eligible hostile creature can be grabbed", grabbed);
    tick(360);
    check("creature resistance releases ownership",
          !physics.HasCargo(actor->GetId()));
    world.RemoveCreature(hostile);
  }
  else
    check("hostile fixture spawned", false);
  physics.Clear();
  actor->CarryHandCount = 0;
  actor->ThrowCharge = 0;
  auto possessedCargo = spawn("rat", feet + forward * 1.8f);
  if (auto *c = world.GetCreature(possessedCargo))
  {
    const auto carrierId = actor->GetId();
    bool grabbed = physics.Grab(
        world, *actor, eye,
        glm::normalize(c->GetCollisionVolume().center - eye), reason);
    bool switched = world.SetControlledCreature(possessedCargo);
    tick(2);
    check("possession of held creature restores movement without duplication",
          grabbed && switched && !c->ExternalMotion &&
              !physics.HasCargo(carrierId) && actor->CarryHandCount == 0);
    world.SetControlledCreature(carrierId);
    actor->SetBodyOrigin(feet);
    camera->SetPosition(eye);
    world.SyncPhysicalCreatureIndex();
    world.RemoveCreature(possessedCargo);
  }
  else
    check("possession fixture spawned", false);
  physics.Clear();
  actor->CarryHandCount = 0;
  actor->ThrowCharge = 0;
  auto deathCargo = spawn("rat", feet + forward * 1.8f);
  if (auto *c = world.GetCreature(deathCargo))
  {
    bool grabbed = physics.Grab(
        world, *actor, eye,
        glm::normalize(c->GetCollisionVolume().center - eye), reason);
    world.SetPlayerDead(true);
    tick(2);
    world.SetPlayerDead(false);
    tick(60);
    check("carrier death releases creature and returns AI ownership",
          grabbed && !physics.HasCargo(actor->GetId()) &&
              actor->CarryHandCount == 0 && world.GetCreature(deathCargo) &&
              !world.GetCreature(deathCargo)->ExternalMotion);
    world.RemoveCreature(deathCargo);
  }
  else
    check("death release fixture spawned", false);
  physics.Clear();
  actor->CarryHandCount = 0;
  auto hiddenCargo = spawn("rat", feet + forward * 2.2f);
  const auto obstruction = floor + glm::ivec3(0, 1, 1);
  world.ReplaceBlockAt(obstruction, "stone");
  if (auto *c = world.GetCreature(hiddenCargo))
  {
    bool grabbed = physics.Grab(
        world, *actor, eye,
        glm::normalize(c->GetCollisionVolume().center - eye), reason);
    check("grab requires direct line of sight",
          !grabbed && !c->ExternalMotion && !physics.HasCargo(actor->GetId()));
  }
  else
    check("line of sight fixture spawned", false);
  world.ReplaceBlockAt(obstruction, "air");
  world.RemoveCreature(hiddenCargo);
  physics.Clear();
  const auto unloadedPosition = feet + glm::vec3(100000, 10, 100000);
  physics.SpawnStack(unloadedPosition, entry("iron_pickaxe", 1, .31f));
  const auto pausedStart = physics.GetItems().front().position;
  tick(60);
  check("unloaded collision region pauses bodies without losing state",
        physics.Count() == 1 &&
            physics.GetItems().front().position == pausedStart &&
            physics.GetItems().front().stack.wear == .31f);
  physics.Clear();
  auto large = spawn("tree_monster", feet + forward * 2.6f);
  if (auto *c = world.GetCreature(large))
  {
    auto hp = c->GetVitals().health;
    bool grabbed = physics.Grab(
        world, *actor, eye,
        glm::normalize(c->GetCollisionVolume().center - eye), reason);
    check("oversized or heavy creature is refused without damage",
          !grabbed &&
              (reason.find("heavy") != std::string::npos ||
               reason.find("large") != std::string::npos) &&
              c->GetVitals().health == hp && !c->ExternalMotion);
    world.RemoveCreature(large);
  }
  else
    check("oversized fixture spawned", false);
  physics.Clear();
  actor->CarryHandCount = 0;
  inv = UCreatureInventory{};
  inv.SetItemDefinitions(world.GetItemDefinitionStorage());
  inv.Insert(entry("wood_bow"));
  inv.Insert(entry("arrow", 8));
  inv.SetActiveSlot(0, 0);
  check("bow fires flying arrow",
        physics.FireArrow(world, *actor, eye, forward, 1.f) &&
            physics.Count() == 1 &&
            inv.Count(InventoryEntryKind::Item, "arrow") == 7);
  tick(60);
  check("arrow stops at wall",
        !physics.GetItems().empty() && !physics.GetItems().front().flying);
  physics.Clear();
  actor->CarryHandCount = 0;
  URecipeRegistry recipes;
  recipes.LoadFromDirectory("content/recipes");
  inv = UCreatureInventory{};
  inv.SetItemDefinitions(world.GetItemDefinitionStorage());
  InventoryEntryRef wood = entry("wood", 8);
  wood.kind = InventoryEntryKind::Block;
  inv.Insert(wood);
  const auto *sticks = recipes.FindById("stick"),
             *woodPick = recipes.FindById("wood_pickaxe");
  check("bootstrap wood to sticks and wooden tool",
        sticks && woodPick && recipes.TryCraft(inv, *sticks) &&
            recipes.TryCraft(inv, *woodPick));
  inv.Insert(entry("raw_iron", 2));
  inv.Insert(entry("charcoal", 2));
  const auto *ingot = recipes.FindById("iron_ingot");
  nlohmann::json before;
  inv.SerializeToJson(before);
  bool refused = ingot && !recipes.TryCraft(inv, *ingot);
  nlohmann::json after;
  inv.SerializeToJson(after);
  check("missing furnace fails atomically", refused && before == after);
  check("smelting converts raw ore and fuel",
        ingot && recipes.TryCraft(inv, *ingot, true) &&
            inv.Count(InventoryEntryKind::Item, "iron_ingot") == 1);
  inv.Insert(entry("iron_hoe"));
  inv.SetActiveSlot(0, 0);
  const auto soil = floor + glm::ivec3(2, 1, 2);
  world.ReplaceBlockAt(soil, "dirt");
  size_t hoeSlot = 0;
  for (size_t n = 0; n < 40; ++n)
    if (inv.GetSlot(n).Id == "iron_hoe")
      hoeSlot = n;
  inv.SetActiveSlot(hoeSlot / 10, hoeSlot % 10);
  const auto soilEye = glm::vec3(soil) + glm::vec3(0, 2, 0);
  survival.Use(world, *actor, soilEye, {0, -1, 0}, reason);
  check("hoe tills soil",
        world.GetBlockRegistry().GetTypeNameById(
            world.GetBlockWorld().GetBlock(soil)) == "farmland_dry");
  inv.Insert(entry("seeds", 2));
  for (size_t n = 0; n < 40; ++n)
    if (inv.GetSlot(n).Id == "seeds")
      inv.SetActiveSlot(n / 10, n % 10);
  survival.Use(world, *actor, soilEye, {0, -1, 0}, reason);
  world.ReplaceBlockAt(soil + glm::ivec3(1, 0, 0), "water");
  for (int n = 0; n < 181; ++n)
    survival.Tick(world, 1.f);
  check("watered planted grain reaches maturity",
        world.GetBlockRegistry().GetTypeNameById(world.GetBlockWorld().GetBlock(
            soil + glm::ivec3(0, 1, 0))) == "cu_crop_ripe");
  nlohmann::json cropSave;
  survival.Serialize(cropSave);
  survival.Deserialize(cropSave);
  nlohmann::json cropReload;
  survival.Serialize(cropReload);
  check("crop persistence round trip", cropSave == cropReload);
  inv = UCreatureInventory{};
  inv.SetItemDefinitions(world.GetItemDefinitionStorage());
  inv.Insert(entry("iron_shield"));
  inv.EquipOffhand(inv.GetSlot(0), *world.GetItemDefinitionStorage());
  auto attacker = spawn("sheep", feet + forward * 1.7f);
  if (auto *source = world.GetCreature(attacker))
  {
    source->AdvanceInfluenceCooldown(2.f);
    PlayerInteractionRouter::SetMeleeIntent(*source, actor->GetId());
    actor->SetBlocking(true);
    auto blocked = InfluenceResolver::Resolve(world, *source,
                                              world.GetGameMode(), nullptr);
    actor->CarryHandCount = 2;
    auto occupied = InfluenceResolver::Resolve(world, *source,
                                               world.GetGameMode(), nullptr);
    check("occupied shield does not protect against melee",
          blocked.Valid && occupied.Valid && !blocked.Targets.empty() &&
              !occupied.Targets.empty() &&
              occupied.Targets[0].HealthDelta < blocked.Targets[0].HealthDelta);
    actor->CarryHandCount = 0;
    actor->SetBlocking(false);
    world.RemoveCreature(attacker);
  }
  else
    check("shield fixture spawned", false);
  float frontalImpactDamage = 0, rearImpactDamage = 0;
  bool shieldFlights = true;
  for (bool frontal : {true, false})
  {
    physics.Clear();
    actor->CarryHandCount = 0;
    inv = UCreatureInventory{};
    inv.SetItemDefinitions(world.GetItemDefinitionStorage());
    inv.Insert(entry("iron_pickaxe"));
    inv.SetActiveSlot(0, 0);
    auto shieldTarget = spawn("sheep", feet + forward * 3.f);
    if (auto *target = world.GetCreature(shieldTarget))
    {
      auto &owned = target->GetInventory();
      owned.Insert(entry("iron_shield"));
      owned.EquipOffhand(owned.GetSlot(0), *world.GetItemDefinitionStorage());
      target->SetBlocking(true);
      target->SetOrientation(frontal ? 180.f : 0.f, 0.f);
      float health = target->GetVitals().health;
      bool armed = physics.BeginThrow(world, *actor, reason);
      tick(1);
      auto cargo = physics.GetItems().empty()
                       ? eye
                       : physics.GetItems().front().position;
      actor->ThrowCharge = 1;
      bool released = physics.Release(
          world, *actor,
          glm::normalize(target->GetCollisionVolume().center - cargo));
      tick(40);
      target = world.GetCreature(shieldTarget);
      float dealt = target ? health - target->GetVitals().health : health;
      if (frontal)
        frontalImpactDamage = dealt;
      else
        rearImpactDamage = dealt;
      shieldFlights = shieldFlights && armed && released && dealt > 0;
      world.RemoveCreature(shieldTarget);
    }
    else
      shieldFlights = false;
  }
  check("physical impacts respect the target's frontal shield arc",
        shieldFlights && frontalImpactDamage < rearImpactDamage);
  physics.Clear();
  actor->CarryHandCount = 0;
  // A moving creature crosses the arrow path; contact must use relative motion.
  auto moving = spawn("sheep", feet + glm::vec3(-1, 0, 4));
  inv = UCreatureInventory{};
  inv.SetItemDefinitions(world.GetItemDefinitionStorage());
  inv.Insert(entry("wood_bow"));
  inv.Insert(entry("arrow", 4));
  inv.SetActiveSlot(0, 0);
  if (auto *target = world.GetCreature(moving))
  {
    const auto hp = target->GetVitals().health;
    const auto aim = glm::normalize(feet + glm::vec3(0, .55f, 4) - eye);
    physics.FireArrow(world, *actor, eye, aim, 1.f);
    for (int frame = 0; frame < 20; ++frame)
    {
      if (auto *live = world.GetCreature(moving))
      {
        live->SetBodyOrigin(feet + glm::vec3(-1 + 8.f * frame / 60, 0, 4));
        live->SetHumanMotionFacts({8, 0, 0}, false, nullptr, 1.f / 60);
        world.SyncPhysicalCreatureIndex();
      }
      tick(1);
    }
    check("moving target is struck by an arrow",
          !world.GetCreature(moving) ||
              world.GetCreature(moving)->GetVitals().health < hp);
    world.RemoveCreature(moving);
  }
  else
    check("moving target fixture spawned", false);
  physics.Clear();
  // Stateful containers use the same inventory instance through
  // fill/pour/drink.
  inv = UCreatureInventory{};
  inv.SetItemDefinitions(world.GetItemDefinitionStorage());
  inv.Insert(entry("bucket"));
  inv.SetActiveSlot(0, 0);
  const auto bucketId = inv.GetSlot(0).instanceId;
  const auto water = floor + glm::ivec3(-2, 1, 1);
  world.ReplaceBlockAt(water, "water");
  const auto waterEye = glm::vec3(water) + glm::vec3(0, 2, 0);
  float emptyMass = physics.Profile(world, inv.GetSlot(0)).mass;
  survival.Use(world, *actor, waterEye, {0, -1, 0}, reason);
  check("bucket collects water and gains mass",
        inv.GetSlot(0).instanceId == bucketId &&
            inv.GetSlot(0).contentsUnits == 1 &&
            physics.Profile(world, inv.GetSlot(0)).mass > emptyMass &&
            world.GetBlockWorld().GetBlock(water) == BLOCK_AIR);
  const auto secondWater = water + glm::ivec3(1, 0, 0);
  world.ReplaceBlockAt(secondWater, "water");
  survival.Use(world, *actor, glm::vec3(secondWater) + glm::vec3(0, 2, 0),
               {0, -1, 0}, reason);
  check("full bucket refuses another source atomically",
        inv.GetSlot(0).instanceId == bucketId &&
            inv.GetSlot(0).contentsUnits == 1 &&
            world.GetBlockRegistry().IsLiquid(
                world.GetBlockWorld().GetBlock(secondWater)));
  world.ReplaceBlockAt(secondWater, "air");
  survival.Use(world, *actor, waterEye, {0, -1, 0}, reason);
  check("bucket pours without losing its instance",
        inv.GetSlot(0).instanceId == bucketId &&
            inv.GetSlot(0).contentsUnits == 0 &&
            world.GetBlockRegistry().IsLiquid(
                world.GetBlockWorld().GetBlock(water)));
  inv = UCreatureInventory{};
  inv.SetItemDefinitions(world.GetItemDefinitionStorage());
  inv.Insert(entry("waterskin"));
  inv.SetActiveSlot(0, 0);
  PlayerInteractionRouter::SetUseIntent(*actor);
  actor->AdvanceInfluenceCooldown(1.f);
  auto emptyDrink =
      InfluenceResolver::Resolve(world, *actor, world.GetGameMode(), nullptr);
  actor->ClearIntent();
  check("empty waterskin cannot restore thirst", emptyDrink.Cancelled);
  survival.Use(world, *actor, waterEye, {0, -1, 0}, reason);
  const auto skinId = inv.GetSlot(0).instanceId;
  actor->GetVitals().thirst = 20.f;
  PlayerInteractionRouter::SetUseIntent(*actor);
  auto drink =
      InfluenceResolver::Resolve(world, *actor, world.GetGameMode(), nullptr);
  InfluenceApplier::Apply(world, drink, world.GetGameMode(), 0.f);
  actor->ClearIntent();
  check("drink consumes finite water, preserves container",
        inv.GetSlot(0).instanceId == skinId &&
            inv.GetSlot(0).contentsUnits == 3 &&
            actor->GetVitals().thirst > 20.f);
  physics.Clear();
  for (int y = 1; y <= 3; ++y)
    world.ReplaceBlockAt(floor + glm::ivec3(-3, y, 6), "water");
  int damped = physics.Spawn(glm::vec3(floor) + glm::vec3(-3, 3.7f, 6), "apple",
                             1, false),
      air = physics.Spawn(glm::vec3(floor) + glm::vec3(3, 3.7f, 6), "apple", 1,
                          false);
  tick(30);
  float wetY = 0, airY = 0;
  for (const auto &body : physics.GetItems())
  {
    if (body.id == damped)
      wetY = body.position.y;
    if (body.id == air)
      airY = body.position.y;
  }
  check("water damps a physical body's fall", wetY > airY + .1f);
  physics.Clear();
  for (int y = 1; y <= 3; ++y)
    world.ReplaceBlockAt(floor + glm::ivec3(-3, y, 6), "air");
  const auto begin = std::chrono::steady_clock::now();
  for (int i = 0; i < 64; ++i)
    physics.Spawn(glm::vec3(floor) + glm::vec3(i % 4, 4, 3 + i / 16), "arrow",
                  1, false);
  tick(60);
  report["physics_64_bodies_60_steps_ms"] =
      std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - begin)
          .count();
  check("body budget retains all property", physics.Count() == 64);
  for (int i = 0; i < 600; ++i)
    physics.Spawn(glm::vec3(floor) + glm::vec3(2, 3, 2), "arrow", 1, false);
  nlohmann::json budgetSave;
  physics.Serialize(budgetSave);
  const auto savedCount = physics.Count();
  physics.Deserialize(budgetSave);
  check("budget overflow is serialized without silent deletion",
        savedCount == 664 && physics.Count() == savedCount);
  const auto fullBegin = std::chrono::steady_clock::now();
  tick(60);
  report["physics_512_active_bodies_60_steps_ms"] =
      std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - fullBegin)
          .count();
  physics.Clear();
  actor->CarryHandCount = 0;
  inv = UCreatureInventory{};
  inv.SetItemDefinitions(world.GetItemDefinitionStorage());
  inv.Insert(entry("iron_pickaxe", 1, .66f));
  inv.SetActiveSlot(0, 0);
  physics.BeginThrow(world, *actor, reason);
  nlohmann::json brokenLink;
  physics.Serialize(brokenLink);
  brokenLink["world_physical_v1"][0]["carrier"] = uint64_t(999999);
  physics.Deserialize(brokenLink);
  tick(2);
  check("missing carrier safely releases cargo and frees hands",
        !physics.HasCargo(actor->GetId()) && actor->CarryHandCount == 0 &&
            physics.Count() == 1 &&
            physics.GetItems().front().stack.wear == .66f);
  physics.Clear();
  inv = UCreatureInventory{};
  inv.SetItemDefinitions(world.GetItemDefinitionStorage());
  inv.Insert(entry("iron_pickaxe", 1, .2f));
  inv.Insert(entry("iron_pickaxe", 1, .7f));
  inv.SetActiveSlot(0, 0);
  auto dig = floor + glm::ivec3(2, 1, 7);
  world.ReplaceBlockAt(dig, "stone");
  world.StartBreakSession(dig, .1f, "iron_pickaxe");
  inv.SetActiveSlot(0, 1);
  check("switching worn instances cancels dig without wear",
        !world.CompleteBreakSession() && inv.GetSlot(0).wear == .2f &&
            inv.GetSlot(1).wear == .7f &&
            world.GetBlockWorld().GetBlock(dig) != BLOCK_AIR);
  world.StartBreakSession(dig, .1f, "iron_pickaxe");
  check("completed dig wears the exact source instance",
        world.CompleteBreakSession() &&
            std::abs(inv.GetSlot(1).wear - .8f) < .001f);
  world.StartBreakSession(dig, .1f, "iron_pickaxe");
  check("failed dig spends no durability",
        !world.CompleteBreakSession() &&
            std::abs(inv.GetSlot(1).wear - .8f) < .001f);
  physics.Clear();
  // Compare the native fixed-step integration under different render deltas.
  nlohmann::json frameRates = nlohmann::json::array();
  glm::vec3 reference{};
  bool same = true;
  for (int fps : {30, 60, 144})
  {
    physics.Clear();
    actor->CarryHandCount = 0;
    actor->ThrowCharge = 0;
    inv = UCreatureInventory{};
    inv.SetItemDefinitions(world.GetItemDefinitionStorage());
    inv.Insert(entry("iron_pickaxe", 1, .23f));
    inv.SetActiveSlot(0, 0);
    bool armed = physics.BeginThrow(world, *actor, reason);
    tick(1);
    actor->ThrowCharge = 1;
    bool released = physics.Release(world, *actor, forward);
    for (int frame = 0; frame < fps; ++frame)
      physics.Tick(world, 1.f / fps);
    glm::vec3 position = physics.GetItems().empty()
                             ? glm::vec3(-999)
                             : physics.GetItems().front().position;
    if (fps == 30)
      reference = position;
    else
      same = same && glm::distance(position, reference) < .02f;
    same = same && armed && released;
    frameRates.push_back(
        {{"fps", fps}, {"position", {position.x, position.y, position.z}}});
  }
  check("native trajectory stable at 30/60/144 FPS", same);
  physics.Clear();
  actor->CarryHandCount = 0;
  inv = UCreatureInventory{};
  inv.SetItemDefinitions(world.GetItemDefinitionStorage());
  inv.Insert(entry("iron_pickaxe", 1, .66f));
  inv.SetActiveSlot(0, 0);
  const auto lagIdentity = inv.GetSlot(0).instanceId;
  bool lagArmed = physics.BeginThrow(world, *actor, reason);
  actor->ThrowCharge = 1;
  bool lagReleased = physics.Release(world, *actor, forward);
  physics.Tick(world, .5f);
  check("long frame keeps a swept body and its exact instance",
        lagArmed && lagReleased && physics.Count() == 1 &&
            physics.GetItems().front().stack.instanceId == lagIdentity &&
            physics.GetItems().front().stack.wear == .66f &&
            physics.GetItems().front().position.z <= float(floor.z + 10));

  report["frame_rates"] = frameRates;
  physics.Clear();
  actor->CarryHandCount = 0;
  world.ReplaceBlockAt(water, "air");
  world.ReplaceBlockAt(soil + glm::ivec3(1, 0, 0), "air");
  if (std::getenv("CUBA_ITEMS_SURVIVAL_LOOP_QA"))
  {
    report["survival_loop"] = RunSurvivalLoopQa(world, floor);
    check("empty-inventory survival integration",
          report["survival_loop"]["passed"].get<bool>());
  }
  const auto savedEffects = actor->GetStatusEffects();
  inv = UCreatureInventory{};
  inv.SetItemDefinitions(world.GetItemDefinitionStorage());
  inv.Insert(entry("iron_pickaxe"));
  inv.SetActiveSlot(0, 0);
  const auto strengthBefore = actor->GetAttributes().strength;
  PlayerInteractionRouter::BeginDigIntent(*actor, floor + glm::ivec3(0, 2, 10));
  const auto normalDig =
      InfluenceResolver::Resolve(world, *actor, world.GetGameMode(), nullptr);
  StatusEffectDef strengthEffect;
  strengthEffect.Id = "qa_strength";
  strengthEffect.DurationSec = 10;
  strengthEffect.StrengthDelta = 10;
  UStatusEffectCatalog::Get().Register(strengthEffect);
  StatusEffectSystem::ApplyStatus(*actor, "qa_strength");
  const auto empoweredDig =
      InfluenceResolver::Resolve(world, *actor, world.GetGameMode(), nullptr);
  check("effective status attributes change real dig capability without "
        "mutating base stats",
        normalDig.Valid && empoweredDig.Valid &&
            empoweredDig.DigDurationSec < normalDig.DigDurationSec &&
            actor->GetAttributes().strength == strengthBefore);
  actor->GetStatusEffects() = savedEffects;
  actor->ClearIntent();
  // Leave a meaningful UI fixture on this QA world for native captures.
  inv = UCreatureInventory{};
  inv.SetItemDefinitions(world.GetItemDefinitionStorage());
  for (const auto *id :
       {"iron_pickaxe", "iron_pickaxe", "wood_sword",  "iron_hoe",
        "wood_bow",     "arrow",        "seeds",       "waterskin",
        "bucket",       "iron_head",    "iron_chest",  "iron_legs",
        "iron_feet",    "wood_shield",  "raw_iron",    "iron_ingot",
        "grain",        "bread",        "cooked_meat", "coal",
        "wool",         "cord"})
    inv.Insert(entry(id, std::string(id) == "arrow" ? 12 : 1,
                     std::string(id) == "iron_pickaxe" ? .42f : 0));
  report["passed"] = true;
  for (const auto &c : report["checks"])
    if (!c["passed"].get<bool>())
      report["passed"] = false;
  std::ofstream file(path);
  file << report.dump(2) << '\n';
}
inline void TickItemsCarryVisualQa(UWorld &world, float elapsed)
{
  static int last = -1;
  static uint64_t creature = 0;
  static bool released = false;
  auto *actor = world.GetControlledCreature();
  auto camera = world.GetCurrentUserCamera();
  if (!actor || !camera)
    return;
  auto &physics = UWorldItemSystem::Instance();
  int phase = std::min(5, int(elapsed / 6.f));
  float time = elapsed - phase * 6.f;
  if (last != phase)
  {
    if (physics.HasCargo(actor->GetId()))
      physics.Release(world, *actor, camera->GetFront(), true);
    if (creature)
      world.RemoveCreature(creature);
    creature = 0;
    physics.Clear();
    actor->CarryHandCount = 0;
    actor->ThrowCharge = 0;
    actor->BowCharge = 0;
    released = false;
    auto &inv = actor->GetInventory();
    inv = UCreatureInventory{};
    inv.SetItemDefinitions(world.GetItemDefinitionStorage());
    inv.SetCreative(false);
    auto e =
        [](std::string id, InventoryEntryKind kind = InventoryEntryKind::Item)
    {
      InventoryEntryRef e;
      e.kind = kind;
      e.Id = id;
      e.count = 1;
      e.empty = false;
      return e;
    };
    std::string reason;
    for (int i = 0;
         i < 3 && camera->GetPerspective() != CameraPerspective::FirstPerson;
         ++i)
      camera->CycleFpsPerspective();
    if (phase < 2)
    {
      inv.Insert(
          e(phase == 0 ? "iron_pickaxe" : "stone",
            phase == 0 ? InventoryEntryKind::Item : InventoryEntryKind::Block));
      inv.SetActiveSlot(0, 0);
      physics.BeginThrow(world, *actor, reason);
    }
    else if (phase < 5)
    {
      world.SetGameMode(WorldGameMode::Creative);
      creature = world.SpawnCreature(phase == 2 ? "rat" : "sheep",
                                     actor->GetBodyOrigin() +
                                         camera->GetFront() * 1.8f,
                                     "", CreatureSpawnSource::Qa);
      world.SetGameMode(WorldGameMode::Survival);
      if (auto *target = world.GetCreature(creature))
      {
        target->GetInventory().SetCreative(false);
        physics.Grab(world, *actor, camera->GetPosition(),
                     glm::normalize(target->GetCollisionVolume().center -
                                    camera->GetPosition()),
                     reason);
      }
      if (phase == 4)
      {
        camera->CycleFpsPerspective();
        camera->CycleFpsPerspective();
      }
    }
    else
    {
      inv.Insert(e("wood_bow"));
      auto arrows = e("arrow");
      arrows.count = 8;
      inv.Insert(arrows);
      inv.SetActiveSlot(0, 0);
    }
    last = phase;
  }
  if (phase < 2)
    actor->ThrowCharge = std::clamp(time / 3.f, 0.f, 1.f);
  else if (phase < 5 && time > 2.f && !released)
  {
    actor->ThrowCharge = 1;
    physics.Release(world, *actor, camera->GetFront() + glm::vec3(0, .1f, 0));
    released = true;
  }
  else if (phase == 5)
    actor->BowCharge = std::clamp(time / 3.f, .01f, 1.f);
}

} // namespace cutum
