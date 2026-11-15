#pragma once
#include "Creatures/Core/Creature.h"
#include "Creatures/Visual/WornEquipmentDrawer.h"
#include "Items/ItemDefinitionStorage.h"
#include "Items/ItemPresentationMath.h"
#include "World/Core/World.h"
#include "World/Items/WorldItemSystem.h"
#include <algorithm>
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
      const auto half = body.physical.size * .5f;
      auto towardCarrier = actor.GetBodyOrigin() - body.position;
      towardCarrier.y = 0;
      glm::vec3 surface(0);
      if (glm::length(towardCarrier) > .0001f)
      {
        towardCarrier = glm::normalize(towardCarrier);
        float distance = 1e9f;
        for (int coordinate : {0, 2})
          if (std::abs(towardCarrier[coordinate]) > .0001f)
            distance =
                std::min(distance, half[coordinate] /
                                       std::abs(towardCarrier[coordinate]));
        surface = towardCarrier * distance;
      }
      // Generic cargo is grasped on its near face, not through its centre.
      // Typed items below keep their individually authored contact points.
      right = glm::clamp(surface + axis * width, -half, half);
      left = glm::clamp(surface - axis * width, -half, half);
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
