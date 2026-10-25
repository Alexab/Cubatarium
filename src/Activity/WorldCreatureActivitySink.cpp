#include "Activity/WorldCreatureActivitySink.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "World/Core/World.h"
#include <unordered_map>

namespace cutum
{

namespace
{

/// Snapshot cache (audit 4.5): GetBehaviorSnapshot used to re-run a
/// string-keyed definition lookup, copy the whole snapshot and reapply the
/// perception aggro multiplier on every agent query. Entries validate
/// against creature/definition pointer identity, current perception and the
/// storage revision, so reloads and stat changes invalidate naturally.
/// Single-threaded world sim; size-capped against slow id churn.
struct CachedSnapshot
{
  const UCreature *creature{nullptr};
  const CreatureDefinition *def{nullptr};
  int perception{0};
  uint64_t defsRevision{0};
  CreatureBehaviorSnapshot snapshot;
};

constexpr size_t kSnapshotCacheMaxEntries = 1024;
std::unordered_map<CreatureId, CachedSnapshot> gBehaviorSnapshotCache;

} // namespace

UWorldCreatureActivitySink::UWorldCreatureActivitySink(UWorld &world)
    : World(world)
{
}

std::optional<CreatureActivityView>
UWorldCreatureActivitySink::GetCreatureView(CreatureId Id) const
{
  const UCreature *creature = World.GetCreature(Id);
  if (!creature)
  {
    return std::nullopt;
  }
  CreatureActivityView view;
  view.Id = creature->GetId();
  view.bodyOrigin = creature->GetBodyOrigin();
  view.typeId = creature->GetTypeId();
  view.possessed = creature->IsPossessed();
  view.isPlayerCharacter = creature->IsPlayerCharacter();
  if (const CreatureDefinition *def =
          World.GetCreatureDefinition(creature->GetTypeId()))
  {
    view.behaviorId = def->behavior.Id;
  }
  return view;
}

std::optional<CreatureBehaviorSnapshot>
UWorldCreatureActivitySink::GetBehaviorSnapshot(CreatureId Id) const
{
  const UCreature *creature = World.GetCreature(Id);
  if (!creature)
  {
    gBehaviorSnapshotCache.erase(Id);
    return std::nullopt;
  }
  const CreatureDefinition *def =
      World.GetCreatureDefinition(creature->GetTypeId());
  if (!def)
  {
    gBehaviorSnapshotCache.erase(Id);
    return std::nullopt;
  }
  const auto &storage = World.GetCreatureDefinitionStorage();
  const uint64_t revision =
      storage ? storage->Revision.load(std::memory_order_relaxed) : 0;
  const int perception = creature->GetAttributes().perception;
  if (const auto it = gBehaviorSnapshotCache.find(Id);
      it != gBehaviorSnapshotCache.end() &&
      it->second.creature == creature && it->second.def == def &&
      it->second.perception == perception &&
      it->second.defsRevision == revision)
  {
    return it->second.snapshot;
  }

  CreatureBehaviorSnapshot snapshot;
  snapshot.behavior = def->behavior;
  snapshot.locomotion = def->locomotion;
  snapshot.habitat = def->habitat;
  snapshot.boundsSize = def->bounds.restSizeBlocks;
  const float perceptionMul =
      0.5f + static_cast<float>(perception) / 20.f;
  snapshot.behavior.aggroRadius *= perceptionMul;

  if (gBehaviorSnapshotCache.size() >= kSnapshotCacheMaxEntries)
  {
    gBehaviorSnapshotCache.clear();
  }
  CachedSnapshot &entry = gBehaviorSnapshotCache[Id];
  entry.creature = creature;
  entry.def = def;
  entry.perception = perception;
  entry.defsRevision = revision;
  entry.snapshot = snapshot;
  return snapshot;
}

void UWorldCreatureActivitySink::SetIntent(CreatureId Id,
                                           const CreatureIntent &intent)
{
  if (UCreature *creature = World.GetCreature(Id))
  {
    creature->SetIntent(intent);
  }
}

} // namespace cutum
