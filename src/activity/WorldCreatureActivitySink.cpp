#include "WorldCreatureActivitySink.h"
#include "World.h"
#include "Creature.h"
#include "CreatureDefinition.h"

namespace cutum {

WorldCreatureActivitySink::WorldCreatureActivitySink(World& world) : world_(world) {}

std::optional<CreatureActivityView> WorldCreatureActivitySink::GetCreatureView(CreatureId id) const
{
 const Creature* creature = world_.GetCreature(id);
 if (!creature) {
  return std::nullopt;
 }
 CreatureActivityView view;
 view.id = creature->GetId();
 view.bodyOrigin = creature->GetBodyOrigin();
 view.typeId = creature->GetTypeId();
 view.possessed = creature->IsPossessed();
 view.isPlayerCharacter = creature->IsPlayerCharacter();
 if (const CreatureDefinition* def = world_.GetCreatureDefinition(creature->GetTypeId())) {
  view.behaviorId = def->behavior.id;
 }
 return view;
}

std::optional<CreatureBehaviorSnapshot> WorldCreatureActivitySink::GetBehaviorSnapshot(
    CreatureId id) const
{
 const Creature* creature = world_.GetCreature(id);
 if (!creature) {
  return std::nullopt;
 }
 const CreatureDefinition* def = world_.GetCreatureDefinition(creature->GetTypeId());
 if (!def) {
  return std::nullopt;
 }
 CreatureBehaviorSnapshot snapshot;
 snapshot.behavior = def->behavior;
 snapshot.locomotion = def->locomotion;
 return snapshot;
}

void WorldCreatureActivitySink::SetIntent(CreatureId id, const CreatureIntent& intent)
{
 if (Creature* creature = world_.GetCreature(id)) {
  creature->SetIntent(intent);
 }
}

} // namespace cutum
