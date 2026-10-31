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

/// The candidate creature when it passes the engagement guard, else null.
const UCreature *EngageableOther(const UWorld &world, CreatureId selfId,
                                 CreatureId candidateId)
{
  if (candidateId == 0 || candidateId == selfId)
  {
    return nullptr;
  }
  const UCreature *self = world.GetCreature(selfId);
  const UCreature *other = world.GetCreature(candidateId);
  if (!self || !other)
  {
    return nullptr;
  }
  if (other->IsPlayerCharacter() || other->IsPossessed())
  {
    return nullptr;
  }
  if (other->GetTypeId() == "bot" || other->GetTypeId() == self->GetTypeId())
  {
    return nullptr;
  }
  const CreatureDefinition *def = DefinitionOf(world, *other);
  if (!def)
  {
    return nullptr;
  }
  if (def->role == CreatureRole::Bot ||
      def->role == CreatureRole::ControlledDefault)
  {
    return nullptr;
  }
  return other;
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

bool IsEngageableMob(const UWorld &world, CreatureId selfId,
                     CreatureId candidateId)
{
  return EngageableOther(world, selfId, candidateId) != nullptr;
}

bool IsMeleeFallbackTarget(const UWorld &world, CreatureId selfId,
                           CreatureId candidateId)
{
  const UCreature *other = EngageableOther(world, selfId, candidateId);
  // No monster infighting: hostile species are not fallback prey.
  return other && !IsHostileSpecies(world, other->GetTypeId());
}

bool IsHostileEngagementTarget(const UWorld &world, CreatureId selfId,
                               CreatureId candidateId)
{
  const UCreature *other = EngageableOther(world, selfId, candidateId);
  return other && IsHostileSpecies(world, other->GetTypeId());
}

} // namespace cutum
