#include "World/Items/WorldItemSystem.h"
#include "Audio/AudioApi.h"
#include "Blocks/BlockRegistry.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include "Creatures/Influence/InfluenceApplier.h"
#include "Creatures/Influence/StatusEffectSystem.h"
#include "Creatures/Visual/WornEquipmentDrawer.h"
#include "Game/Inventory/InventoryIdentity.h"
#include "Game/Inventory/InventoryOperations.h"
#include "Game/ModePolicy.h"
#include "Game/Survival/SurvivalSystems.h"
#include "Items/ItemDefinitionStorage.h"
#include "Items/ToolCapabilities.h"
#include "World/Core/World.h"
#include "World/Math/GridMath.h"
#include "World/Physics/CollisionReadiness.h"
#include <climits>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <unordered_map>
#include <unordered_set>
namespace cutum
{
namespace
{
nlohmann::json SerializePhysicalBody(const WorldItem &i)
{
  const auto &e = i.stack;
  return {
      {"id", i.id},
      {"item", e.Id},
      {"kind", int(e.kind)},
      {"count", e.count},
      {"wear", e.wear},
      {"broken", e.broken},
      {"instance", e.instanceId},
      {"contents", e.contentsId},
      {"units", e.contentsUnits},
      {"pinned", e.pinned},
      {"creature", i.creatureId},
      {"carrier", i.carrierId},
      {"thrower", i.throwerId},
      {"position", {i.position.x, i.position.y, i.position.z}},
      {"velocity", {i.velocity.x, i.velocity.y, i.velocity.z}},
      {"mass", i.physical.mass},
      {"hardness", i.physical.hardness},
      {"size", {i.physical.size.x, i.physical.size.y, i.physical.size.z}},
      {"last_contact", i.lastContact},
      {"contact_cooldown", i.contactCooldown},
      {"age", i.age},
      {"held_time", i.heldTime},
      {"charge", i.charge},
      {"motion", i.carrierId ? "held"
                 : i.flying  ? "flight"
                             : "resting"},
      {"flying", i.flying},
      {"arrow", i.arrow},
      {"damage", i.damage},
      {"domain",
       i.domain == InventoryDomain::Creative ? "creative" : "survival"},
      {"action_domain",
       i.ActionDomain == InventoryDomain::Creative ? "creative" : "survival"},
      {"carrier_domain",
       i.CarrierDomain == InventoryDomain::Creative ? "creative" : "survival"},
      {"pickup_separated", i.pickupSeparated}};
}
} // namespace
UWorldItemSystem &UWorldItemSystem::Instance()
{
  static UWorldItemSystem s;
  return s;
}
bool UWorldItemSystem::ReserveDrops(size_t count)
{
  try
  {
    Items.reserve(512);
    Pending.reserve(512);
    Dormant.reserve(Dormant.size() + count);
    return true;
  }
  catch (const std::bad_alloc &)
  {
    return false;
  }
}
int UWorldItemSystem::Add(WorldItem item)
{
  if (!item.creatureId && !item.stack.empty)
    item.stack.instanceId = InventoryInstanceIdentity(item.stack.instanceId);
  item.id = NextId++;
  DormantDirty = true;
  int id = item.id;
  if (item.carrierId && !Ticking && Items.size() >= 512)
  {
    auto candidate =
        std::find_if(Items.begin(), Items.end(),
                     [](const WorldItem &body) { return !body.carrierId; });
    if (candidate != Items.end())
    {
      Dormant.push_back(std::move(*candidate));
      Items.erase(candidate);
    }
  }
  if (Items.size() >= 512)
    Dormant.push_back(std::move(item));
  else if (Ticking && Pending.size() < 512)
    Pending.push_back(std::move(item));
  else if (Ticking)
    Dormant.push_back(std::move(item));
  else
    Items.push_back(std::move(item));
  return id;
}
bool UWorldItemSystem::DropFromActor(UWorld &world, UCreature &actor,
                                     const InventoryEntryRef &entry,
                                     std::string &reason)
{
  if (entry.empty || entry.IsCatalogReference())
  {
    reason = "A finite physical payload is required";
    return false;
  }
  if (!CanActivate() || !ReserveDrops(1))
  {
    reason = "Physical body budget is busy";
    return false;
  }
  const auto profile = Profile(world, entry);
  auto origin = actor.GetCollisionVolume().center;
  origin.y = std::max(origin.y,
                      actor.GetBodyOrigin().y + profile.size.y * .5f + .005f);
  glm::vec3 eye, direction(0, 0, 1);
  world.GetControlledViewRay(eye, direction);
  const auto occupied = Sweep(world, origin, glm::vec3(0), profile.size * .5f,
                              0, actor.GetId(), 0);
  if (!occupied.ready || occupied.found)
  {
    reason = "No safe space to drop this item";
    return false;
  }
  const auto movement = direction * .9f;
  const auto path =
      Sweep(world, origin, movement, profile.size * .5f, 0, actor.GetId(), 0);
  if (!path.ready)
  {
    reason = "Collision region is not ready";
    return false;
  }
  WorldItem item;
  item.stack = entry;
  item.position = origin + movement * std::max(0.f, path.time - .005f);
  item.physical = profile;
  item.flying = true;
  item.domain = DomainForMode(world.GetGameMode());
  item.ActionDomain = item.domain;
  item.throwerId = actor.GetId();
  item.pickupSeparated = false;
  return Add(item) != 0;
}
int UWorldItemSystem::Spawn(glm::vec3 const &position, const std::string &id,
                            int count, bool block, InventoryDomain domain)
{
  InventoryEntryRef e;
  e.empty = false;
  e.kind = block ? InventoryEntryKind::Block : InventoryEntryKind::Item;
  e.Id = id;
  e.count = count;
  return SpawnStack(position, e, domain);
}
int UWorldItemSystem::SpawnStack(glm::vec3 position, InventoryEntryRef e,
                                 InventoryDomain domain, uint64_t thrower)
{
  if (e.empty || e.IsCatalogReference() || e.Id.empty() || e.count <= 0)
    return 0;
  WorldItem i;
  i.stack = std::move(e);
  i.position = position + (i.stack.kind == InventoryEntryKind::Block
                               ? glm::vec3(0)
                               : glm::vec3(0, .2f, 0));
  i.flying = true;
  i.domain = domain;
  i.ActionDomain = domain;
  i.CarrierDomain = domain;
  i.throwerId = thrower;
  i.pickupSeparated = thrower == 0;
  return Add(i);
}
PhysicalProfile UWorldItemSystem::Profile(UWorld &world,
                                          const InventoryEntryRef &e) const
{
  if (e.kind == InventoryEntryKind::Item)
  {
    if (const auto *defs = world.GetItemDefinitionStorage())
      if (const auto *d = defs->Get(e.Id))
      {
        auto p = d->Physical;
        p.mass += e.contentsUnits * (e.Id == "bucket" ? 4.f : .25f);
        return p;
      }
  }
  PhysicalProfile p;
  p.size = glm::vec3(1);
  p.mass = e.Id.find("wood") != std::string::npos ||
                   e.Id.find("log") != std::string::npos
               ? 4.f
               : 12.f;
  if (e.Id.find("steel") != std::string::npos ||
      e.Id.find("metal") != std::string::npos)
    p.mass = 40.f;
  return p;
}
PhysicalProfile UWorldItemSystem::Profile(UWorld &world,
                                          const UCreature &creature) const
{
  PhysicalProfile p;
  p.size = creature.GetCollisionSize();
  p.mass = std::max(.1f, p.size.x * p.size.y * p.size.z * 14.f);
  for (const auto &row : creature.GetInventory().GetHotbars())
    for (const auto &s : row.slots)
      if (!s.empty && !s.entry.empty && !s.entry.IsCatalogReference() &&
          s.entry.count > 0)
        p.mass += Profile(world, s.entry).mass * s.entry.count;
  for (const auto &e : creature.GetInventory().GetRecovery())
    if (!e.empty && !e.IsCatalogReference() && e.count > 0)
      p.mass += Profile(world, e).mass * e.count;
  for (size_t i = 0; i < 4; ++i)
  {
    const auto &e = creature.GetInventory().GetEquippedArmor(i);
    if (!e.empty)
      p.mass += Profile(world, e).mass;
  }
  const auto &off = creature.GetInventory().GetEquippedOffhand();
  if (!off.empty)
    p.mass += Profile(world, off).mass;
  return p;
}
void UWorldItemSystem::HandleDeath(UWorld &world, UCreature &creature)
{
  if (creature.IsPlayerCharacter() || !Deaths.insert(creature.GetId()).second)
    return;
  const auto domain = DamageDomain.value_or(DomainForMode(world.GetGameMode()));
  if (const auto *def = world.GetCreatureDefinition(creature.GetTypeId()))
    for (const auto &drop : def->drops)
      Spawn(creature.GetBodyOrigin(), drop.itemId, drop.count, false, domain);
  auto inventory = creature.GetInventory();
  for (bool creative : {false, true})
  {
    inventory.SetCreative(creative);
    const auto ownDomain =
        creative ? InventoryDomain::Creative : InventoryDomain::Survival;
    auto spill = [&](const InventoryEntryRef &e)
    {
      if (!e.empty && !e.IsCatalogReference())
        SpawnStack(creature.GetBodyOrigin(), e, ownDomain);
    };
    for (size_t slot = 0; slot < UCreatureInventory::kSlotCount; ++slot)
      spill(inventory.GetSlot(slot));
    for (size_t slot = 0; slot < 4; ++slot)
      spill(inventory.GetEquippedArmor(slot));
    spill(inventory.GetEquippedOffhand());
    for (const auto &e : inventory.GetRecovery())
      spill(e);
  }
}
void UWorldItemSystem::ReconcileOwnership(UWorld &world)
{
  // Normalize unavailable definitions once at the load boundary, through the
  // versioned inventory representation. Each bank keeps its own recovery.
  world.ForEachCreature(
      [&](UCreature &creature)
      {
        auto &inventory = creature.GetInventory();
        nlohmann::json state;
        inventory.SerializeToJson(state);
        bool changed = false;
        const auto *defs = world.GetItemDefinitionStorage();
        auto normalize = [&](nlohmann::json &entry, const char *recovery)
        {
          if (entry.value("empty", true))
            return;
          const auto kind = entry.value("kind", std::string("block"));
          const auto id = entry.value("id", std::string{});
          const bool known =
              kind == "item" ? defs && defs->Get(id)
              : kind == "block"
                  ? world.GetBlockRegistry().GetPackBlockIdByTypeName(id) !=
                        BLOCK_AIR
                  : true;
          if (known)
            return;
          state[recovery].push_back(entry);
          entry = {{"empty", true}};
          state["inventory_migration_warnings"].push_back(
              "unknown_definition:" + id);
          changed = true;
        };
        for (const auto &pair : {std::pair<const char *, const char *>{
                                     "owned_slots", "inventory_recovery"},
                                 {"creative_slots", "creative_recovery"},
                                 {"equipped_armor", "inventory_recovery"},
                                 {"creative_armor", "creative_recovery"}})
          for (auto &entry : state[pair.first])
            normalize(entry, pair.second);
        normalize(state["equipped_offhand"], "inventory_recovery");
        normalize(state["creative_offhand"], "creative_recovery");
        if (changed)
          inventory.DeserializeFromJson(state);
      });
  std::unordered_set<uint64_t> owned;
  world.ForEachCreature(
      [&](const UCreature &creature)
      {
        auto inv = creature.GetInventory();
        for (bool creative : {false, true})
        {
          inv.SetCreative(creative);
          auto remember = [&](const InventoryEntryRef &e)
          {
            if (!e.empty && !e.IsCatalogReference() && e.instanceId)
              owned.insert(e.instanceId);
          };
          for (size_t n = 0; n < 40; ++n)
            remember(inv.GetSlot(n));
          for (size_t n = 0; n < 4; ++n)
            remember(inv.GetEquippedArmor(n));
          remember(inv.GetEquippedOffhand());
          for (const auto &e : inv.GetRecovery())
            remember(e);
        }
      });
  auto validate = [&](auto &list)
  {
    for (auto it = list.begin(); it != list.end();)
    {
      bool duplicate = false;
      bool ambiguous = false;
      if (!it->creatureId)
      {
        const auto *defs = world.GetItemDefinitionStorage();
        ambiguous = (it->stack.kind == InventoryEntryKind::Item &&
                     (!defs || !defs->Get(it->stack.Id))) ||
                    (it->stack.kind == InventoryEntryKind::Block &&
                     world.GetBlockRegistry().GetPackBlockIdByTypeName(
                         it->stack.Id) == BLOCK_AIR);
        if (it->stack.instanceId && !owned.insert(it->stack.instanceId).second)
        {
          ambiguous = true;
          duplicate = true;
        }
      }
      if (ambiguous)
      {
        auto record = SerializePhysicalBody(*it);
        record["recovery_reason"] =
            duplicate ? "duplicate_instance" : "unknown_definition";
        PhysicalRecovery.push_back(record);
        it = list.erase(it);
      }
      else
        ++it;
    }
  };
  validate(Items);
  validate(Pending);
  validate(Dormant);
  if (!PhysicalRecovery.empty())
  {
    Feedback = "Unresolved world property preserved in recovery";
    FeedbackTime = 10.f;
  }
}
void UWorldItemSystem::ReleaseCarrier(UWorld &world, uint64_t carrier)
{
  auto *c = world.GetCreature(carrier);
  if (!c)
    return;
  Release(world, *c, glm::vec3(0, 0, 1), true);
}
bool UWorldItemSystem::HasCargo(uint64_t id) const
{
  for (const auto &i : Items)
    if (i.carrierId == id)
      return true;
  return false;
}
UWorldItemSystem::Hit UWorldItemSystem::Sweep(UWorld &world, glm::vec3 p,
                                              glm::vec3 delta, glm::vec3 half,
                                              uint64_t skip, uint64_t exclude,
                                              float dt, uint64_t contact) const
{
  Hit result;
  const auto low = glm::min(p, p + delta) - half,
             high = glm::max(p, p + delta) + half;
  for (int x = int(std::floor(low.x + .5f)); x <= int(std::floor(high.x + .5f));
       ++x)
    for (int z = int(std::floor(low.z + .5f));
         z <= int(std::floor(high.z + .5f)); ++z)
      for (int y = int(std::floor(low.y + .5f));
           y <= int(std::floor(high.y + .5f)); ++y)
      {
        if (!IsCollisionRingReady(world.GetBlockWorld(), {x, y, z}, 0))
        {
          result.ready = false;
          return result;
        }
        const auto block = world.GetBlockWorld().GetBlock({x, y, z});
        if (!world.GetBlockRegistry().BlocksMovement(block))
          continue;
        auto hit =
            SweepBody(p, delta, half, glm::vec3(x, y, z), glm::vec3(.5f));
        if (hit && hit->time <= result.time)
        {
          result.time = hit->time;
          result.normal = hit->normal;
          result.creature = 0;
          result.found = true;
        }
      }
  CollisionVolume volume;
  volume.center = (low + high) * .5f;
  volume.halfExtents =
      (high - low) * .5f + glm::vec3(1.f + MaxTargetSpeed * dt);
  for (const auto &n :
       world.QueryCreatureNeighborsOverlappingVolume(volume, skip))
  {
    if (n.Id == exclude || n.Id == contact)
      continue;
    auto *target = world.GetCreature(n.Id);
    if (!target)
      continue;
    const auto v = target->GetCollisionVolume();
    const auto movement = target->GetLocomotionFacts().actualVelocity * dt;
    auto hit =
        SweepBody(p, delta, half, v.center - movement, v.halfExtents, movement);
    if (hit && hit->time < result.time)
    {
      result.time = hit->time;
      result.normal = hit->normal;
      result.creature = n.Id;
      result.found = true;
    }
  }
  return result;
}
bool UWorldItemSystem::Grab(UWorld &world, UCreature &actor, glm::vec3 eye,
                            glm::vec3 direction, std::string &reason)
{
  if (HasCargo(actor.GetId()))
    return Release(world, actor, direction, true);
  if (!ReserveDrops(1))
  {
    reason = "Cannot reserve carry storage";
    return false;
  }
  if (Items.size() >= 512)
  {
    reason = "Physical body budget is busy; try again shortly";
    return false;
  }
  if (actor.BowCharge > 0)
  {
    reason = "Hands are occupied by the bow";
    return false;
  }
  if (actor.ExternalMotion)
  {
    reason = "Cannot grab while carried";
    return false;
  }
  const auto id = world.PickCreatureByView(eye, direction, 2.6f);
  if (id && *id != actor.GetId())
  {
    auto *target = world.GetCreature(*id);
    if (!target || target->ExternalMotion || target->IsPossessed() ||
        HasCargo(*id))
    {
      reason = "Target is already controlled";
      return false;
    }
    auto p = Profile(world, *target);
    int hands = CarryHands(
        p, StatusEffectSystem::GetEffectiveAttributes(actor).strength,
        actor.GetCollisionSize().y);
    if (!hands)
    {
      reason = "Target is too heavy or too large";
      return false;
    }
    WorldItem i;
    i.creatureId = *id;
    i.carrierId = actor.GetId();
    i.CarrierDomain = DomainForMode(world.GetGameMode());
    i.physical = p;
    i.position = target->GetCollisionVolume().center;
    auto path = Sweep(world, eye, i.position - eye, glm::vec3(.02f),
                      actor.GetId(), *id, 0);
    if (path.found || !path.ready)
    {
      reason = "Target is obstructed";
      return false;
    }
    target->ExternalMotion = true;
    target->ClearIntent();
    target->GetLocomotion().BreakGroundSupport();
    actor.CarryHandCount = hands;
    actor.SetBlocking(false);
    Add(i);
    return true;
  }
  if (Items.size() < 512)
  {
    for (auto it = Dormant.begin(); it != Dormant.end(); ++it)
    {
      auto d = it->position - eye;
      auto along = glm::dot(d, direction);
      if (along > 0 && along < 2.6f && glm::length(d - direction * along) < .6f)
      {
        Items.push_back(std::move(*it));
        Dormant.erase(it);
        break;
      }
    }
  }
  WorldItem *nearest = nullptr;
  float closest = 2.6f;
  for (auto &i : Items)
    if (!i.creatureId && !i.carrierId)
    {
      auto d = i.position - eye;
      float along = glm::dot(d, direction);
      if (along > 0 && along < closest &&
          glm::length(d - direction * along) < .6f)
      {
        nearest = &i;
        closest = along;
      }
    }
  if (!nearest)
  {
    reason = "No physical target in reach";
    return false;
  }
  nearest->physical = Profile(world, nearest->stack);
  int hands =
      CarryHands(nearest->physical,
                 StatusEffectSystem::GetEffectiveAttributes(actor).strength,
                 actor.GetCollisionSize().y);
  if (!hands)
  {
    reason = "Item is too heavy or too large";
    return false;
  }
  auto path = Sweep(world, eye, nearest->position - eye, glm::vec3(.02f),
                    actor.GetId(), 0, 0);
  if (path.found || !path.ready)
  {
    reason = "Item is obstructed";
    return false;
  }
  if (nearest->stack.count > 1)
  {
    auto rest = *nearest;
    rest.stack.count--;
    rest.id = NextId++;
    if (Pending.size() < 512)
      Pending.push_back(rest);
    else
    {
      Dormant.push_back(rest);
      DormantDirty = true;
    }
    nearest->stack.count = 1;
    nearest->stack.instanceId = InventoryInstanceIdentity(0);
  }
  nearest->carrierId = actor.GetId();
  nearest->CarrierDomain = DomainForMode(world.GetGameMode());
  nearest->flying = false;
  nearest->heldTime = 0;
  nearest->charge = 0;
  actor.CarryHandCount = hands;
  actor.SetBlocking(false);
  return true;
}
bool UWorldItemSystem::CanThrow(UWorld &world, const InventoryEntryRef &entry,
                                std::string &reason) const
{
  ThrowCategory category = ThrowCategory::General;
  if (entry.kind == InventoryEntryKind::Block)
    category = ThrowCategory::Block;
  else if (entry.kind == InventoryEntryKind::Item)
  {
    const auto *defs = world.GetItemDefinitionStorage();
    const auto *def = defs ? defs->Get(entry.Id) : nullptr;
    if (def)
      category = def->ThrowClass;
  }
  else
  {
    reason = "This entry is not a physical item";
    return false;
  }
  if (!world.GetThrowSettings().Allows(category))
  {
    reason = "Throwing this category is disabled in world settings";
    return false;
  }
  return true;
}
bool UWorldItemSystem::BeginThrow(UWorld &world, UCreature &actor,
                                  std::string &reason)
{
  if (actor.ExternalMotion || actor.BowCharge > 0)
  {
    reason = "Hands are occupied";
    return false;
  }
  for (const auto &cargo : Items)
    if (cargo.carrierId == actor.GetId() && !cargo.creatureId &&
        !CanThrow(world, cargo.stack, reason))
      return false;
  if (!HasCargo(actor.GetId()))
  {
    if (Items.size() >= 512)
    {
      reason = "Physical body budget is busy; try again shortly";
      return false;
    }
    auto &inv = actor.GetInventory();
    auto *active = inv.GetActiveEntryRef();
    if (!active || (active->kind != InventoryEntryKind::Item &&
                    active->kind != InventoryEntryKind::Block))
    {
      reason = "Hold a physical item";
      return false;
    }
    if (!CanThrow(world, *active, reason))
      return false;
    const auto p = Profile(world, *active);
    int hands = CarryHands(
        p, StatusEffectSystem::GetEffectiveAttributes(actor).strength,
        actor.GetCollisionSize().y);
    if (!hands)
    {
      reason = "Item is too heavy or too large";
      return false;
    }
    if (!ReserveDrops(1))
    {
      reason = "Cannot reserve physical storage";
      return false;
    }
    auto result = UInventoryOperations::TransferToWorld(
        inv, inv.GetActiveBarIndex() * 10 + inv.GetActiveSlotIndex(), 1,
        [&](const InventoryEntryRef &unit)
        {
          WorldItem i;
          i.stack = unit;
          i.physical = p;
          i.carrierId = actor.GetId();
          i.CarrierDomain = DomainForMode(world.GetGameMode());
          i.position = actor.GetEyePosition() + glm::vec3(0, -.3f, 0);
          i.domain = DomainForMode(world.GetGameMode());
          i.pickupSeparated = false;
          return Add(i) != 0;
        });
    if (!result)
    {
      reason = result.reason;
      return false;
    }
    actor.CarryHandCount = hands;
    actor.SetBlocking(false);
  }
  world.CancelBreakSession();
  actor.ClearIntent();
  SetThrowCharge(actor, .001f);
  return true;
}
void UWorldItemSystem::SetThrowCharge(UCreature &actor, float charge)
{
  const float value =
      std::isfinite(charge) ? std::clamp(charge, 0.f, 1.f) : 0.f;
  bool found = false;
  for (auto *list : {&Items, &Pending, &Dormant})
    for (auto &body : *list)
      if (body.carrierId == actor.GetId())
      {
        body.charge = value;
        found = true;
      }
  actor.ThrowCharge = found ? value : 0.f;
}
void UWorldItemSystem::CancelCharge(UCreature &actor)
{
  SetThrowCharge(actor, 0);
}
bool UWorldItemSystem::Release(UWorld &world, UCreature &actor,
                               glm::vec3 direction, bool gentle)
{
  const float magnitude = glm::length(direction);
  if (!std::isfinite(magnitude) || magnitude < 1e-5f)
    return false;
  direction /= magnitude;
  for (auto &i : Items)
    if (i.carrierId == actor.GetId())
    {
      if (!gentle && !i.creatureId)
      {
        std::string reason;
        if (!CanThrow(world, i.stack, reason))
        {
          CancelCharge(actor);
          return false;
        }
      }
      i.ActionDomain = DomainForMode(world.GetGameMode());
      if (i.creatureId)
        i.domain = DomainForMode(world.GetGameMode());
      float speed =
          gentle
              ? 0
              : ThrowSpeed(
                    i.physical,
                    StatusEffectSystem::GetEffectiveAttributes(actor).strength,
                    i.charge, actor.GetCollisionSize().y);
      i.charge = 0;
      if (gentle)
      {
        auto ground =
            Sweep(world, i.position, {0, -2.5f, 0}, i.physical.size * .5f,
                  i.creatureId, actor.GetId(), 0);
        if (ground.ready && ground.found && ground.normal.y > .5f)
        {
          i.position +=
              glm::vec3(0, -2.5f, 0) * std::max(0.f, ground.time - .001f);
          i.carrierId = 0;
          i.throwerId = actor.GetId();
          i.age = 0;
          i.heldTime = 0;
          Settle(world, i);
          if (auto *c = world.GetCreature(i.creatureId))
            c->SetBodyOrigin(i.position -
                             glm::vec3(0, i.physical.size.y * .5f, 0));
          actor.CarryHandCount = 0;
          actor.ThrowCharge = 0;
          return true;
        }
      }
      i.carrierId = 0;
      i.throwerId = actor.GetId();
      i.pickupSeparated = false;
      i.velocity = direction * speed;
      i.flying = true;
      i.age = 0;
      i.heldTime = 0;
      actor.CarryHandCount = 0;
      actor.ThrowCharge = 0;
      actor.HumanMotion.actionRemaining = .28f;
      if (world.GetGameMode() == WorldGameMode::Survival)
        actor.GetVitals().fatigue =
            std::min(actor.GetVitals().maxFatigue,
                     actor.GetVitals().fatigue + i.physical.mass * .3f);
      return true;
    }
  return false;
}
bool UWorldItemSystem::FireArrow(UWorld &world, UCreature &actor, glm::vec3 eye,
                                 glm::vec3 direction, float charge)
{
  if (!CanActivate() || !ReserveDrops(1))
    return false;
  if (actor.CarryHandCount > 0 || actor.ExternalMotion)
    return false;
  const float magnitude = glm::length(direction);
  if (!std::isfinite(magnitude) || magnitude < 1e-5f || !std::isfinite(charge))
    return false;
  direction /= magnitude;
  charge = std::clamp(charge, 0.f, 1.f);
  auto &inv = actor.GetInventory();
  auto *active = inv.GetActiveEntryRef();
  auto *defs = world.GetItemDefinitionStorage();
  const auto *bow = active && defs ? defs->Get(active->Id) : nullptr;
  if (!bow || !bow->Ranged.Enabled || active->broken || charge < .1f)
    return false;
  auto trial = inv;
  InventoryEntryRef projectile;
  if (inv.IsCreative())
  {
    projectile.empty = false;
    projectile.kind = InventoryEntryKind::Item;
    projectile.Id = bow->Ranged.AmmoId;
    projectile.count = 1;
  }
  else
  {
    size_t slot = UCreatureInventory::kSlotCount;
    for (size_t n = 0; n < UCreatureInventory::kSlotCount; ++n)
    {
      const auto &entry = trial.GetSlot(n);
      if (!entry.empty && entry.kind == InventoryEntryKind::Item &&
          entry.Id == bow->Ranged.AmmoId && !entry.IsCatalogReference())
      {
        slot = n;
        break;
      }
    }
    if (slot == UCreatureInventory::kSlotCount ||
        !trial.Take(slot, 1, projectile))
      return false;
  }
  WorldItem i;
  i.stack = projectile;
  i.physical = Profile(world, i.stack);
  i.physical.size = glm::vec3(.08f);
  i.throwerId = actor.GetId();
  i.position = eye + direction * .65f;
  i.velocity = direction * (12.f + charge * 22.f);
  i.arrow = true;
  i.flying = true;
  i.domain = DomainForMode(world.GetGameMode());
  i.ActionDomain = i.domain;
  i.pickupSeparated = false;
  i.damage =
      std::max(1, bow->Tool.Damage.FleshyOrMelee()) * (.3f + .7f * charge);
  const auto hit = Sweep(world, eye, i.position - eye, i.physical.size * .5f,
                         actor.GetId(), 0, 0);
  if (!hit.ready || hit.found)
    return false;
  auto *tool = trial.GetActiveEntryRef();
  if (tool)
    ApplyItemWear(
        *tool, *bow, 1.f / std::max(1, bow->Tool.PunchAttackUses),
        IsToolWearEnabled(world.GetGameMode(), world.GetDifficulty()));
  inv = std::move(trial);
  Add(i);
  UAudioEvent shot;
  shot.Kind = AudioEventKind::RangedShot;
  shot.X = eye.x;
  shot.Y = eye.y;
  shot.Z = eye.z;
  shot.EntityId = static_cast<uint32_t>(actor.GetId());
  UAudioApi::Post(shot);
  return true;
}
void UWorldItemSystem::Settle(UWorld &world, WorldItem &i)
{
  i.velocity = {};
  i.flying = false;
  if (i.creatureId)
    if (auto *c = world.GetCreature(i.creatureId))
    {
      c->ExternalMotion = false;
      c->GetLocomotion().Reset();
      c->GetLocomotion().BreakGroundSupport();
    }
}
void UWorldItemSystem::MoveHeldCargo(UWorld &world, WorldItem &item,
                                     glm::vec3 desired, float dt)
{
  auto movement = desired - item.position;
  // A blocked grip slides along surfaces instead of letting go. The next step
  // retries from the last safe position, so looking up can lift floor contact.
  for (int plane = 0; plane < 3; ++plane)
  {
    const float distance = glm::length(movement);
    if (distance < .0001f)
      return;
    const auto hit =
        Sweep(world, item.position, movement, item.physical.size * .5f,
              item.creatureId, item.carrierId, dt);
    if (!hit.ready)
      return;
    item.position += movement * std::max(0.f, hit.time - .001f / distance);
    if (!hit.found)
      return;
    movement = desired - item.position;
    movement -= hit.normal * std::min(0.f, glm::dot(movement, hit.normal));
  }
}
void UWorldItemSystem::Step(UWorld &world, float dt)
{
  auto *actor = world.GetControlledCreature();
  for (auto &i : Items)
  {
    i.age += dt;
    i.contactCooldown = std::max(0.f, i.contactCooldown - dt);
    if (actor &&
        glm::distance(actor->GetCollisionVolume().center, i.position) > 1.85f)
      i.pickupSeparated = true;
    auto *creature = i.creatureId ? world.GetCreature(i.creatureId) : nullptr;
    if (i.creatureId && !creature)
    {
      if (auto *h = world.GetCreature(i.carrierId))
      {
        h->CarryHandCount = 0;
        h->ThrowCharge = 0;
      }
      i.id = 0;
      continue;
    }
    if (creature && creature == actor)
    {
      if (auto *h = world.GetCreature(i.carrierId))
      {
        h->CarryHandCount = 0;
        h->ThrowCharge = 0;
      }
      Settle(world, i);
      i.id = 0;
      continue;
    }
    if (creature)
      creature->ExternalMotion = true;
    if (i.carrierId)
    {
      auto *holder = world.GetCreature(i.carrierId);
      if (!holder || holder != actor || world.IsPlayerDead())
      {
        if (holder)
        {
          holder->CarryHandCount = 0;
          holder->ThrowCharge = 0;
        }
        i.carrierId = 0;
        i.flying = true;
        i.velocity = {};
        continue;
      }
      if (i.CarrierDomain != DomainForMode(world.GetGameMode()))
      {
        Release(world, *holder, glm::vec3(0, 0, 1), true);
        continue;
      }
      glm::vec3 viewEye, viewForward;
      if (!world.GetControlledViewRay(viewEye, viewForward))
        continue;
      int hands = CarryHands(
          i.physical,
          StatusEffectSystem::GetEffectiveAttributes(*holder).strength,
          holder->GetCollisionSize().y);
      holder->CarryHandCount = hands;
      holder->SetBlocking(false);
      std::string throwReason;
      if (!i.creatureId && holder->ThrowCharge > 0 &&
          !CanThrow(world, i.stack, throwReason))
        CancelCharge(*holder);
      i.heldTime += dt;
      if (i.charge > 0)
        i.charge = std::min(1.f, i.charge + dt / 1.1f);
      holder->ThrowCharge = i.charge;
      if (world.GetGameMode() == WorldGameMode::Survival)
        holder->GetVitals().fatigue =
            std::min(holder->GetVitals().maxFatigue,
                     holder->GetVitals().fatigue + dt * i.physical.mass * .08f);
      float resistance =
          creature
              ? std::clamp(
                    2.f + .15f * (StatusEffectSystem::GetEffectiveAttributes(
                                      *holder)
                                      .strength -
                                  StatusEffectSystem::GetEffectiveAttributes(
                                      *creature)
                                      .strength),
                    .5f, 5.f)
              : 100000.f;
      if (!hands || i.heldTime > resistance ||
          (world.GetGameMode() == WorldGameMode::Survival &&
           holder->GetVitals().fatigue >= holder->GetVitals().maxFatigue))
      {
        Release(world, *holder, viewForward, true);
        continue;
      }
      float distance =
          .85f + .65f * std::max(i.physical.size.x, i.physical.size.z);
      const auto desired =
          viewEye + viewForward * (distance - .22f * holder->ThrowCharge) +
          glm::vec3(
              0, -.45f - .25f * i.physical.size.y + .12f * holder->ThrowCharge,
              0);
      // Only a genuinely separated carrier loses the grip. A quick camera
      // turn can move the requested contact across the full carry diameter.
      if (glm::distance(viewEye, i.position) > 4.f)
      {
        i.carrierId = 0;
        i.flying = true;
        i.velocity = {};
        holder->CarryHandCount = 0;
        holder->ThrowCharge = 0;
        continue;
      }
      MoveHeldCargo(world, i, desired, dt);
    }
    else if (i.flying || !i.creatureId)
    {
      const auto half = i.physical.size * .5f;
      const auto delta = i.velocity * dt + glm::vec3(0, -10.f * dt * dt, 0);
      uint64_t excluded = i.age < .25f ? i.throwerId : 0;
      if (auto *owner = world.GetCreature(i.throwerId);
          owner && glm::dot(i.position - owner->GetCollisionVolume().center,
                            i.velocity) >= 0)
        excluded = i.throwerId;
      auto hit = Sweep(world, i.position, delta, half, i.creatureId, excluded,
                       dt, i.contactCooldown > 0 ? i.lastContact : 0);
      if (!hit.ready)
        continue;
      i.position += delta * std::max(0.f, hit.time - .001f);
      i.velocity.y -= 20.f * dt;
      if (hit.found)
      {
        float normalSpeed = std::max(0.f, -glm::dot(i.velocity, hit.normal));
        if (auto *target = world.GetCreature(hit.creature))
          normalSpeed = std::max(
              0.f, -glm::dot(i.velocity -
                                 target->GetLocomotionFacts().actualVelocity,
                             hit.normal));
        float mass = i.physical.mass;
        if (hit.creature)
          if (auto *other = world.GetCreature(hit.creature))
          {
            float m = Profile(world, *other).mass;
            mass = mass * m / (mass + m);
          }
        float damage =
            i.arrow ? i.damage
                    : ImpactDamage(mass, normalSpeed, i.physical.hardness);
        auto sourcePos = i.position + hit.normal * .01f;
        if (hit.creature)
          InfluenceApplier::ApplyImpact(world, hit.creature, i.throwerId,
                                        damage, sourcePos, i.ActionDomain);
        if (i.creatureId)
          InfluenceApplier::ApplyImpact(
              world, i.creatureId, i.throwerId, ImpactDamage(mass, normalSpeed),
              i.position - hit.normal, i.ActionDomain);
        i.lastContact = hit.creature;
        i.contactCooldown = .25f;
        if (creature || hit.normal.y > .5f)
          Settle(world, i);
        else
        {
          i.velocity = {};
          i.flying = true;
        }
        if (i.arrow)
        {
          i.arrow = false;
          i.age = 0;
        }
      }
      else
      {
        i.flying = true;
        auto fluid = world.SampleFluidPhysicsVolume({i.position, half});
        if (fluid.inFluid)
          i.velocity *= std::exp(-5.f * dt);
      }
    }
    if (creature)
    {
      if (auto *live = world.GetCreature(i.creatureId))
      {
        live->SetBodyOrigin(i.position -
                            glm::vec3(0, i.physical.size.y * .5f, 0));
        if (!i.carrierId && !i.flying)
          i.id = 0;
      }
    }
    if (!i.creatureId && !i.carrierId && !i.flying && i.age > .7f && actor &&
        glm::distance(actor->GetBodyOrigin(), i.position) < 1.75f)
    {
      auto &inv = actor->GetInventory();
      if (i.domain != DomainForMode(world.GetGameMode()))
      {
        Feedback = i.domain == InventoryDomain::Creative
                       ? "Creative item: switch mode to pick up"
                       : "Survival item: switch mode to pick up";
        FeedbackTime = 2;
        continue;
      }
      if (!i.pickupSeparated && i.throwerId == actor->GetId())
        continue;
      inv.SetItemDefinitions(world.GetItemDefinitionStorage());
      auto accepted = inv.Insert(i.stack);
      Feedback =
          accepted.transferred
              ? "+" + std::to_string(accepted.transferred) + " " + i.stack.Id
              : "Backpack full: item remains in world";
      FeedbackTime = 2.f;
      if (accepted.transferred)
      {
        i.stack.count = accepted.remainder;
        if (!accepted.remainder)
          i.id = 0;
        else
          i.stack.instanceId = 0;
      }
    }
  }
  Items.erase(std::remove_if(Items.begin(), Items.end(),
                             [](const auto &i) { return i.id == 0; }),
              Items.end());
}
void UWorldItemSystem::Tick(UWorld &world, float dt)
{
  FeedbackTime = std::max(0.f, FeedbackTime - std::max(0.f, dt));
  if (FeedbackTime == 0)
    Feedback.clear();
  auto *actor = world.GetControlledCreature();
  if (actor && !HasCargo(actor->GetId()))
  {
    actor->CarryHandCount = 0;
    actor->ThrowCharge = 0;
  }
  std::unordered_set<uint64_t> bodies;
  for (const auto &i : Items)
    if (i.creatureId)
      bodies.insert(i.creatureId);
  for (const auto &i : Dormant)
    if (i.creatureId)
      bodies.insert(i.creatureId);
  for (const auto &i : Pending)
    if (i.creatureId)
      bodies.insert(i.creatureId);
  MaxTargetSpeed = 0;
  world.ForEachCreature(
      [&](UCreature &c)
      {
        const float speed = glm::length(c.GetLocomotionFacts().actualVelocity);
        if (std::isfinite(speed))
          MaxTargetSpeed = std::max(MaxTargetSpeed, speed);
        if (c.ExternalMotion && !bodies.count(c.GetId()))
        {
          c.ExternalMotion = false;
          c.GetLocomotion().Reset();
          c.ClearIntent();
        }
      });

  SleepProbeClock += std::max(0.f, dt);
  const bool probeSupport = SleepProbeClock >= .25f;
  if (probeSupport)
    SleepProbeClock = 0;
  // Sleeping/deferred records do not consume the active simulation budget.
  for (auto it = Dormant.begin(); it != Dormant.end();)
  {
    it->age += std::max(0.f, dt);
    if (actor &&
        glm::distance(actor->GetCollisionVolume().center, it->position) > 1.85f)
      it->pickupSeparated = true;
    if (actor && !it->flying && !it->carrierId && it->age > .7f &&
        it->domain == DomainForMode(world.GetGameMode()) &&
        (it->pickupSeparated || it->throwerId != actor->GetId()) &&
        glm::distance(actor->GetBodyOrigin(), it->position) < 1.75f)
    {
      auto accepted = actor->GetInventory().Insert(it->stack);
      Feedback =
          accepted.transferred
              ? "+" + std::to_string(accepted.transferred) + " " + it->stack.Id
              : "Backpack full: item remains in world";
      FeedbackTime = 2.f;
      if (accepted.transferred)
      {
        it->stack.count = accepted.remainder;
        if (accepted.remainder)
          it->stack.instanceId = 0;
      }
      if (!it->stack.count)
      {
        it = Dormant.erase(it);
        continue;
      }
    }
    if (probeSupport && !it->flying && actor &&
        glm::distance(actor->GetBodyOrigin(), it->position) < 48.f)
    {
      const auto support = Sweep(world, it->position, {0, -.04f, 0},
                                 it->physical.size * .5f, 0, actor->GetId(), 0);
      if (support.ready && !support.found)
        it->flying = true;
    }
    if (Items.size() < 512 &&
        (it->carrierId || it->creatureId ||
         (it->flying && actor &&
          glm::distance(actor->GetBodyOrigin(), it->position) < 48.f)))
    {
      Items.push_back(std::move(*it));
      it = Dormant.erase(it);
    }
    else
      ++it;
  }
  while (Items.size() < 512 && !Pending.empty())
  {
    Items.push_back(std::move(Pending.back()));
    Pending.pop_back();
  }
  for (auto &i : Items)
    if (!i.creatureId && i.physical.size == glm::vec3(.25f))
      i.physical = Profile(world, i.stack);
  Accumulator = std::min(.15f, Accumulator + std::max(0.f, dt));
  Ticking = true;
  while (Accumulator >= 1.f / 60)
  {
    Step(world, 1.f / 60);
    Accumulator -= 1.f / 60;
  }
  Ticking = false;
  for (auto it = Items.begin(); it != Items.end();)
  {
    if (!it->creatureId && !it->carrierId && !it->flying)
    {
      Dormant.push_back(std::move(*it));
      DormantDirty = true;
      it = Items.erase(it);
    }
    else
      ++it;
  }
  if (DormantDirty)
    CoalesceDormant(world);
  if (!Items.empty())
    world.SyncPhysicalCreatureIndex();
}
void UWorldItemSystem::CoalesceDormant(UWorld &world)
{
  DormantDirty = false;
  std::unordered_map<std::string, size_t> groups;
  std::vector<WorldItem> compact;
  compact.reserve(Dormant.size());
  const auto *defs = world.GetItemDefinitionStorage();
  for (auto &body : Dormant)
  {
    const auto *def = body.stack.kind == InventoryEntryKind::Item && defs
                          ? defs->Get(body.stack.Id)
                          : nullptr;
    const int limit = body.stack.kind == InventoryEntryKind::Block ? 64
                      : def ? def->StackMax
                            : 1;
    if (body.creatureId || body.carrierId || body.throwerId ||
        body.stack.pinned || limit <= 1 || body.velocity != glm::vec3(0))
    {
      compact.push_back(std::move(body));
      continue;
    }
    auto keyRow = SerializePhysicalBody(body);
    for (const char *key :
         {"id", "instance", "count", "age", "held_time", "charge", "motion"})
      keyRow.erase(key);
    const auto key = keyRow.dump();
    const auto found = groups.find(key);
    if (found != groups.end() &&
        compact[found->second].stack.count <= INT_MAX - body.stack.count)
    {
      auto &target = compact[found->second];
      target.stack.count += body.stack.count;
      target.age = std::min(target.age, body.age);
    }
    else
    {
      groups[key] = compact.size();
      compact.push_back(std::move(body));
    }
  }
  Dormant = std::move(compact);
}
void UWorldItemSystem::Draw(UWorld &world, CreatureDrawQueue &queue,
                            const glm::mat4 &vp)
{
  auto draw = [&](const WorldItem &i)
  {
    if (!i.creatureId)
    {
      auto model = glm::translate(glm::mat4(1), i.position);
      if (i.flying && glm::length(i.velocity) > .01f)
        model *= glm::mat4_cast(
            [](glm::vec3 d)
            {
              const glm::vec3 up(0, 1, 0);
              if (d.y < -.999f)
                return glm::angleAxis(3.14159265f, glm::vec3(1, 0, 0));
              return glm::normalize(glm::quat(1.f + d.y, glm::cross(up, d)));
            }(glm::normalize(i.velocity)));
      if (!i.flying && !i.carrierId)
        model = glm::rotate(model, i.age * .4f, glm::vec3(0, 1, 0));
      if (auto *defs = world.GetItemDefinitionStorage())
        if (auto *d = defs->Get(i.stack.Id))
          model = glm::scale(model, glm::vec3(d->Visual.GroundScale));
      WornEquipmentDrawer::SubmitLoose(
          queue, i.stack.Id, vp, model, Profile(world, i.stack).size,
          world.GetBlockRegistry().GetBlockAtlasTexture(
              world.GetBlockRegistry().GetIdByTypeName(i.stack.Id)));
    }
  };
  for (const auto &i : Items)
    draw(i);
  for (const auto &i : Dormant)
    draw(i);
}
void UWorldItemSystem::Clear()
{
  Items.clear();
  Pending.clear();
  Dormant.clear();
  Deaths.clear();
  PhysicalRecovery = nlohmann::json::array();
  Accumulator = 0;
  SleepProbeClock = 0;
  Feedback.clear();
  FeedbackTime = 0;
  NextId = 1;
}
void UWorldItemSystem::Serialize(nlohmann::json &root) const
{
  auto rows = nlohmann::json::array();
  auto add = [&](const auto &list)
  {
    for (const auto &i : list)
    {
      rows.push_back(SerializePhysicalBody(i));
    }
  };
  add(Items);
  add(Pending);
  add(Dormant);
  root["world_physical_v2"] = rows;
  root["physical_deaths"] = Deaths;
  root["physical_recovery"] = PhysicalRecovery;
}
void UWorldItemSystem::Deserialize(const nlohmann::json &root)
{
  Clear();
  const char *key = root.contains("world_physical_v2") ? "world_physical_v2"
                                                       : "world_physical_v1";
  if (!root.contains(key) || !root[key].is_array())
    return;
  std::unordered_set<uint64_t> bodies, holders;
  Deaths = root.value("physical_deaths", std::unordered_set<uint64_t>{});
  auto rows = root[key];
  if (root.contains("physical_recovery") &&
      root["physical_recovery"].is_array())
    for (const auto &row : root["physical_recovery"])
      rows.push_back(row);
  for (const auto &j : rows)
  {
    try
    {
      const auto recoveryReason = j.value("recovery_reason", std::string{});
      if (recoveryReason == "duplicate_instance" ||
          recoveryReason == "duplicate_entity")
      {
        PhysicalRecovery.push_back(j);
        continue;
      }
      WorldItem i;
      i.stack.Id = j.value("item", "");
      i.stack.kind = InventoryEntryKind(std::clamp(j.value("kind", 4), 0, 4));
      i.stack.empty = i.stack.Id.empty();
      i.stack.count = std::max(1, j.value("count", 1));
      i.stack.wear = std::clamp(j.value("wear", 0.f), 0.f, 1.f);
      i.stack.broken = j.value("broken", false);
      i.stack.instanceId = j.value("instance", uint64_t{0});
      i.stack.contentsId = j.value("contents", "");
      i.stack.contentsUnits = std::max(0, j.value("units", 0));
      i.stack.pinned = j.value("pinned", false);
      i.creatureId = j.value("creature", uint64_t{0});
      i.carrierId = j.value("carrier", uint64_t{0});
      i.throwerId = j.value("thrower", uint64_t{0});
      for (int a = 0; a < 3; ++a)
      {
        i.position[a] = j.at("position").at(a).get<float>();
        i.velocity[a] = j.at("velocity").at(a).get<float>();
        i.physical.size[a] =
            std::clamp(j.at("size").at(a).get<float>(), .02f, 5.f);
      }
      i.physical.hardness = std::clamp(j.value("hardness", 1.f), 0.f, 4.f);
      i.physical.mass = std::clamp(j.value("mass", 1.f), .01f, 100000.f);
      i.lastContact = j.value("last_contact", uint64_t{0});
      i.contactCooldown = j.value("contact_cooldown", 0.f);
      i.age = j.value("age", 0.f);
      i.heldTime = j.value("held_time", 0.f);
      // Input key state is not restored: cancel windup without releasing cargo.
      i.charge = 0;
      i.flying = j.value("flying", false);
      i.arrow = j.value("arrow", false);
      i.damage = j.value("damage", 0.f);
      i.domain = j.value("domain", j.value("creative_copy", false)
                                       ? std::string("creative")
                                       : std::string("survival")) == "creative"
                     ? InventoryDomain::Creative
                     : InventoryDomain::Survival;
      i.ActionDomain =
          j.value("action_domain", i.domain == InventoryDomain::Creative
                                       ? std::string("creative")
                                       : std::string("survival")) == "creative"
              ? InventoryDomain::Creative
              : InventoryDomain::Survival;
      i.CarrierDomain =
          j.value("carrier_domain", i.domain == InventoryDomain::Creative
                                        ? std::string("creative")
                                        : std::string("survival")) == "creative"
              ? InventoryDomain::Creative
              : InventoryDomain::Survival;
      i.pickupSeparated = j.value("pickup_separated", true);
      if ((i.creatureId && (!i.stack.empty || i.arrow)) ||
          (!i.creatureId && (i.stack.empty ||
                             (i.stack.kind != InventoryEntryKind::Item &&
                              i.stack.kind != InventoryEntryKind::Block) ||
                             j.value("count", 1) <= 0)))
      {
        PhysicalRecovery.push_back(j);
        continue;
      }
      bool valid = true;
      for (int axis = 0; axis < 3; ++axis)
        valid = valid && std::isfinite(i.position[axis]) &&
                std::isfinite(i.velocity[axis]) &&
                std::isfinite(i.physical.size[axis]);
      if (!valid || !std::isfinite(i.physical.mass) || i.physical.mass <= 0 ||
          glm::any(glm::lessThanEqual(i.physical.size, glm::vec3(0))))
      {
        PhysicalRecovery.push_back(j);
        continue;
      }
      if (i.creatureId && !bodies.insert(i.creatureId).second)
      {
        auto duplicate = j;
        duplicate["recovery_reason"] = "duplicate_entity";
        PhysicalRecovery.push_back(duplicate);
        continue;
      }
      if (i.carrierId &&
          (!holders.insert(i.carrierId).second || i.carrierId == i.creatureId))
      {
        i.carrierId = 0;
        i.flying = true;
        i.velocity = {};
      }
      if (i.creatureId || !i.stack.empty)
        Add(i);
    }
    catch (const nlohmann::json::exception &)
    {
      PhysicalRecovery.push_back(j);
    }
  }
}
} // namespace cutum
