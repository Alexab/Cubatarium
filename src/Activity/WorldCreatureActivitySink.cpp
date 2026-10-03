#include "Activity/WorldCreatureActivitySink.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include "World/Core/World.h"

namespace cutum
{

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
    return std::nullopt;
  }
  const CreatureDefinition *def =
      World.GetCreatureDefinition(creature->GetTypeId());
  if (!def)
  {
    return std::nullopt;
  }
  CreatureBehaviorSnapshot snapshot;
  snapshot.behavior = def->behavior;
  snapshot.locomotion = def->locomotion;
  snapshot.habitat = def->habitat;
  snapshot.boundsSize = def->bounds.restSizeBlocks;
  const float perceptionMul =
      0.5f + static_cast<float>(creature->GetAttributes().perception) / 20.f;
  snapshot.behavior.aggroRadius *= perceptionMul;
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
