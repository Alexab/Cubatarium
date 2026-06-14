#include "WorldCreatureActivitySink.h"
#include "World.h"
#include "Creature.h"
#include "CreatureDefinition.h"

namespace cutum {

UWorldCreatureActivitySink::UWorldCreatureActivitySink(UWorld& world) : World(world) {}

std::optional<CreatureActivityView> UWorldCreatureActivitySink::GetCreatureView(CreatureId id) const
{
 const UCreature* creature = World.GetCreature(id);
 if (!creature) {
  return std::nullopt;
 }
 CreatureActivityView view;
 view.id = creature->GetId();
 view.bodyOrigin = creature->GetBodyOrigin();
 view.typeId = creature->GetTypeId();
 view.possessed = creature->IsPossessed();
 view.isPlayerCharacter = creature->IsPlayerCharacter();
 if (const CreatureDefinition* def = World.GetCreatureDefinition(creature->GetTypeId())) {
  view.behaviorId = def->behavior.id;
 }
 return view;
}

std::optional<CreatureBehaviorSnapshot> UWorldCreatureActivitySink::GetBehaviorSnapshot(
    CreatureId id) const
{
 const UCreature* creature = World.GetCreature(id);
 if (!creature) {
  return std::nullopt;
 }
 const CreatureDefinition* def = World.GetCreatureDefinition(creature->GetTypeId());
 if (!def) {
  return std::nullopt;
 }
 CreatureBehaviorSnapshot snapshot;
 snapshot.behavior = def->behavior;
 snapshot.locomotion = def->locomotion;
 return snapshot;
}

void UWorldCreatureActivitySink::SetIntent(CreatureId id, const CreatureIntent& intent)
{
 if (UCreature* creature = World.GetCreature(id)) {
  creature->SetIntent(intent);
 }
}

} // namespace cutum
