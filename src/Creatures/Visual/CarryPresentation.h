#pragma once
#include "Creatures/Core/Creature.h"
#include "Creatures/Visual/WornEquipmentDrawer.h"
#include "Items/ItemDefinitionStorage.h"
#include "Items/ItemPresentationMath.h"
#include "World/Core/World.h"
#include "World/Items/WorldItemSystem.h"
#include <cmath>

namespace cutum
{
inline glm::vec3 CarrierRight(const UCreature &actor)
{
  return ItemCarrierRight(actor.GetYaw());
}
/// Read-only projection of the authoritative physical cargo for both FP and TP.
inline bool ResolveCarryContacts(UWorld &world, const UCreature &actor,
                                 glm::vec3 &right, glm::vec3 &left)
{
  if (actor.CarryHandCount <= 0)
    return false;
  const auto axis = CarrierRight(actor);
  for (const auto &body : UWorldItemSystem::Instance().GetActiveItems())
    if (body.carrierId == actor.GetId())
    {
      const float width = glm::dot(glm::abs(axis), body.physical.size) * .5f *
                          (body.creatureId ? .72f : 1.f);
      right = axis * width;
      left = -right;
      if (!body.creatureId)
      {
        const auto *items = world.GetItemDefinitionStorage();
        const auto *definition = items ? items->Get(body.stack.Id) : nullptr;
        WornEquipmentDrawer::CarriedContacts(
            body.stack.Id, axis,
            definition ? definition->Visual.GroundScale : 1.f,
            actor.CarryHandCount, right, left);
      }
      right += body.position;
      left += body.position;
      return true;
    }
  return false;
}
} // namespace cutum
