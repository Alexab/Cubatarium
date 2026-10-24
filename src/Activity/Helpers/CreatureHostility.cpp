#include "Activity/Helpers/CreatureHostility.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include "World/Core/World.h"

namespace cutum
{

namespace
{

const CreatureDefinition *DefinitionOf(const UWorld &world,
                                       const UCreature &creature)
{
  return world.GetCreatureDefinition(creature.GetTypeId());
}

bool HasMonsterTag(const CreatureDefinition &def)
{
  for (const auto &tag : def.catalog.tags)
  {
    if (tag == "hostile" || tag == "monster" || tag == "mobs_monster")
    {
      return true;
    }
  }
  return false;
}

} // namespace

bool IsHostileSpecies(const UWorld &world, const std::string &typeId)
{
  const CreatureDefinition *def = world.GetCreatureDefinition(typeId);
  if (!def)
  {
    return false;
  }
  return def->behavior.Id == "melee_attack" || HasMonsterTag(*def);
}

bool IsMeleeFallbackTarget(const UWorld &world, CreatureId selfId,
                           CreatureId candidateId)
{
  if (candidateId == 0 || candidateId == selfId)
  {
    return false;
  }
  const UCreature *self = world.GetCreature(selfId);
  const UCreature *other = world.GetCreature(candidateId);
  if (!self || !other)
  {
    return false;
  }
  if (other->IsPlayerCharacter() || other->IsPossessed())
  {
    return false;
  }
  if (other->GetTypeId() == "bot" || other->GetTypeId() == self->GetTypeId())
  {
    return false;
  }
  const CreatureDefinition *def = DefinitionOf(world, *other);
  if (!def)
  {
    return false;
  }
  if (def->role == CreatureRole::Bot ||
      def->role == CreatureRole::ControlledDefault)
  {
    return false;
  }
  // No monster infighting: hostile species are not fallback prey.
  return !IsHostileSpecies(world, other->GetTypeId());
}

} // namespace cutum
