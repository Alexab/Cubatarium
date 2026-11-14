#include "World/Items/WorldItemSystem.h"
#include "Audio/AudioApi.h"
#include "Blocks/BlockRegistry.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Influence/InfluenceApplier.h"
#include "Creatures/Influence/StatusEffectSystem.h"
#include "Creatures/Visual/WornEquipmentDrawer.h"
#include "Game/Survival/SurvivalSystems.h"
#include "Items/ItemDefinitionStorage.h"
#include "Items/ToolCapabilities.h"
#include "World/Core/World.h"
#include "World/Math/GridMath.h"
#include "World/Physics/CollisionReadiness.h"
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <unordered_set>
namespace cutum
{
UWorldItemSystem &UWorldItemSystem::Instance()
{
  static UWorldItemSystem s;
  return s;
}
int UWorldItemSystem::Add(WorldItem item)
{
  item.id = NextId++;
  int id = item.id;
  if (Ticking || Items.size() >= 512)
    Pending.push_back(std::move(item));
  else
    Items.push_back(std::move(item));
  return id;
}
int UWorldItemSystem::Spawn(glm::vec3 const &position, const std::string &id,
                            int count, bool block)
{
  InventoryEntryRef e;
  e.empty = false;
  e.kind = block ? InventoryEntryKind::Block : InventoryEntryKind::Item;
  e.Id = id;
  e.count = count;
  return SpawnStack(position, e);
}
int UWorldItemSystem::SpawnStack(glm::vec3 position, InventoryEntryRef e,
                                 bool copy)
{
  if (e.empty || e.Id.empty() || e.count <= 0)
    return 0;
  WorldItem i;
  i.stack = std::move(e);
  i.position = position + glm::vec3(0, .2f, 0);
  i.creativeCopy = copy;
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
  for (const auto &row : creature.GetInventory().GetOwnedHotbars())
    for (const auto &s : row.slots)
      if (!s.empty && !s.entry.empty && s.entry.count > 0)
        p.mass += Profile(world, s.entry).mass * s.entry.count;
  for (const auto &e : creature.GetInventory().GetRecovery())
    if (!e.empty && e.count > 0)
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
                                              float dt) const
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
    if (n.Id == exclude)
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
    Pending.push_back(rest);
    nearest->stack.count = 1;
    nearest->stack.instanceId = 0;
  }
  nearest->carrierId = actor.GetId();
  nearest->flying = false;
  nearest->heldTime = 0;
  actor.CarryHandCount = hands;
  actor.SetBlocking(false);
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
    const auto p = Profile(world, *active);
    int hands = CarryHands(
        p, StatusEffectSystem::GetEffectiveAttributes(actor).strength,
        actor.GetCollisionSize().y);
    if (!hands)
    {
      reason = "Item is too heavy or too large";
      return false;
    }
    InventoryEntryRef unit;
    if (!inv.Take(inv.GetActiveBarIndex() * 10 + inv.GetActiveSlotIndex(), 1,
                  unit))
      return false;
    WorldItem i;
    i.stack = unit;
    i.physical = p;
    i.carrierId = actor.GetId();
    i.position = actor.GetEyePosition() + glm::vec3(0, -.3f, 0);
    i.creativeCopy = inv.IsCreative();
    Add(i);
    actor.CarryHandCount = hands;
    actor.SetBlocking(false);
  }
  world.CancelBreakSession();
  actor.ClearIntent();
  actor.ThrowCharge = .001f;
  return true;
}
void UWorldItemSystem::CancelCharge(UCreature &actor) { actor.ThrowCharge = 0; }
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
      float speed =
          gentle
              ? 0
              : ThrowSpeed(
                    i.physical,
                    StatusEffectSystem::GetEffectiveAttributes(actor).strength,
                    actor.ThrowCharge, actor.GetCollisionSize().y);
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
  if (!trial.Consume(InventoryEntryKind::Item, bow->Ranged.AmmoId))
    return false;
  WorldItem i;
  i.stack.empty = false;
  i.stack.Id = bow->Ranged.AmmoId;
  i.stack.kind = InventoryEntryKind::Item;
  i.stack.count = 1;
  i.physical = Profile(world, i.stack);
  i.physical.size = glm::vec3(.08f);
  i.throwerId = actor.GetId();
  i.position = eye + direction * .65f;
  i.velocity = direction * (12.f + charge * 22.f);
  i.arrow = true;
  i.flying = true;
  i.creativeCopy = inv.IsCreative();
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
      glm::vec3 viewEye, viewForward;
      if (!world.GetControlledViewRay(viewEye, viewForward))
        continue;
      int hands = CarryHands(
          i.physical,
          StatusEffectSystem::GetEffectiveAttributes(*holder).strength,
          holder->GetCollisionSize().y);
      holder->CarryHandCount = hands;
      holder->SetBlocking(false);
      i.heldTime += dt;
      if (holder->ThrowCharge > 0)
        holder->ThrowCharge = std::min(1.f, holder->ThrowCharge + dt / 1.1f);
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
      auto hit =
          Sweep(world, i.position, delta, half, i.creatureId, excluded, dt);
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
                                        damage, sourcePos);
        if (i.creatureId)
          InfluenceApplier::ApplyImpact(world, i.creatureId, i.throwerId,
                                        ImpactDamage(mass, normalSpeed),
                                        i.position - hit.normal);
        Settle(world, i);
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
      if (i.creativeCopy)
      {
        i.id = 0;
        continue;
      }
      auto &inv = actor->GetInventory();
      if (inv.IsCreative())
        continue;
      inv.SetItemDefinitions(world.GetItemDefinitionStorage());
      auto accepted = inv.Insert(i.stack);
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
  if (auto *actor = world.GetControlledCreature();
      actor && !HasCargo(actor->GetId()))
  {
    actor->CarryHandCount = 0;
    actor->ThrowCharge = 0;
  }
  std::unordered_set<uint64_t> bodies;
  for (const auto &i : Items)
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
  if (!Items.empty())
    world.SyncPhysicalCreatureIndex();
}
void UWorldItemSystem::Draw(UWorld &world, CreatureDrawQueue &queue,
                            const glm::mat4 &vp)
{
  for (const auto &i : Items)
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
}
void UWorldItemSystem::Clear()
{
  USurvivalSystems::Instance().Clear();
  Items.clear();
  Pending.clear();
  Accumulator = 0;
  NextId = 1;
}
void UWorldItemSystem::Serialize(nlohmann::json &root) const
{
  auto rows = nlohmann::json::array();
  auto add = [&](const auto &list)
  {
    for (const auto &i : list)
    {
      const auto &e = i.stack;
      rows.push_back(
          {{"id", i.id},
           {"item", e.Id},
           {"kind", int(e.kind)},
           {"count", e.count},
           {"wear", e.wear},
           {"broken", e.broken},
           {"instance", e.instanceId},
           {"contents", e.contentsId},
           {"units", e.contentsUnits},
           {"creature", i.creatureId},
           {"carrier", i.carrierId},
           {"thrower", i.throwerId},
           {"position", {i.position.x, i.position.y, i.position.z}},
           {"velocity", {i.velocity.x, i.velocity.y, i.velocity.z}},
           {"mass", i.physical.mass},
           {"hardness", i.physical.hardness},
           {"size", {i.physical.size.x, i.physical.size.y, i.physical.size.z}},
           {"age", i.age},
           {"held_time", i.heldTime},
           {"flying", i.flying},
           {"arrow", i.arrow},
           {"damage", i.damage},
           {"creative_copy", i.creativeCopy}});
    }
  };
  add(Items);
  add(Pending);
  root["world_physical_v1"] = rows;
}
void UWorldItemSystem::Deserialize(const nlohmann::json &root)
{
  Clear();
  if (!root.contains("world_physical_v1") ||
      !root["world_physical_v1"].is_array())
    return;
  std::unordered_set<uint64_t> bodies, holders;
  for (const auto &j : root["world_physical_v1"])
  {
    try
    {
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
      i.age = j.value("age", 0.f);
      i.heldTime = j.value("held_time", 0.f);
      i.flying = j.value("flying", false);
      i.arrow = j.value("arrow", false);
      i.damage = j.value("damage", 0.f);
      i.creativeCopy = j.value("creative_copy", false);
      bool valid = true;
      for (int axis = 0; axis < 3; ++axis)
        valid = valid && std::isfinite(i.position[axis]) &&
                std::isfinite(i.velocity[axis]) &&
                std::isfinite(i.physical.size[axis]);
      if (!valid || !std::isfinite(i.physical.mass))
        continue;
      if (i.creatureId && !bodies.insert(i.creatureId).second)
        continue;
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
    }
  }
}
} // namespace cutum
