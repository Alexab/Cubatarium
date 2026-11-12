#include "World/Interaction/BlockPlacementService.h"

#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureInventory.h"
#include "Creatures/Player/PlayerCapsule.h"
#include "Game/Economy/ResourceEconomy.h"
#include "Game/Inventory/InventoryTypes.h"
#include "Render/Camera/Camera.h"
#include "World/Collision/WorldCollision.h"
#include "World/Core/World.h"
#include "World/Core/WorldViewBinding.h"
#include "World/Math/BlockTypes.h"
#include "World/Math/GridMath.h"

namespace cutum
{

bool UBlockPlacementService::AddObjectByView(UWorld &world,
                                             const glm::vec3 &position,
                                             const glm::vec3 &front)
{
  auto user = world.GetCurrentUser();
  if (!user)
  {
    return false;
  }

  UCreature *controlled = world.GetControlledCreature();
  if (!controlled)
  {
    return false;
  }
  const InventoryEntryRef *activeEntry =
      controlled->GetInventory().GetActiveEntryRef();
  if (!activeEntry || activeEntry->empty ||
      activeEntry->kind != InventoryEntryKind::Block ||
      activeEntry->Id.empty())
  {
    return false;
  }
  const std::string &blockType = activeEntry->Id;
  const InventoryEntryRef entryCopy = *activeEntry;
  if (!ResourceEconomyService::CanPlace(world.GetGameMode(),
                                         controlled->GetInventory(),
                                         entryCopy))
  {
    return false;
  }

  PlayerCapsule cap =
      world.ViewBinding ? world.ViewBinding->ResolvePlacementCapsule(world)
                        : PlayerCapsule::Standing();

  float max_distance = 8.0f;
  glm::vec3 player_eye = position;
  if (auto camera = world.GetCurrentUserCamera())
  {
    max_distance = camera->GetBlockInteractMaxDistance();
    player_eye = camera->GetPosition();
  }
  const BlockPlacementResolve resolved = world.Collision.ResolveBlockPlacement(
      position, front, cap, max_distance, player_eye);
  if (!resolved.place_block_pos)
  {
    return false;
  }
  if (world.AddObject(blockType, BlockCenter(*resolved.place_block_pos)))
  {
    (void)ResourceEconomyService::ConsumeOnPlace(world.GetGameMode(),
                                                  controlled->GetInventory(),
                                                  entryCopy);
    world.UpdateIntersection(position, front);
    return true;
  }
  return false;
}

bool UBlockPlacementService::PlaceBlockFromHeldItem(
    UWorld &world, const glm::vec3 &position, const glm::vec3 &front,
    const std::string &blockId)
{
  UCreature *controlled = world.GetControlledCreature();
  if (!controlled || blockId.empty())
  {
    return false;
  }
  PlayerCapsule cap =
      world.ViewBinding ? world.ViewBinding->ResolvePlacementCapsule(world)
                        : PlayerCapsule::Standing();
  float max_distance = 8.0f;
  glm::vec3 player_eye = position;
  if (auto camera = world.GetCurrentUserCamera())
  {
    max_distance = camera->GetBlockInteractMaxDistance();
    player_eye = camera->GetPosition();
  }
  const BlockPlacementResolve resolved = world.Collision.ResolveBlockPlacement(
      position, front, cap, max_distance, player_eye);
  if (!resolved.place_block_pos)
  {
    return false;
  }
  if (!world.AddObject(blockId, BlockCenter(*resolved.place_block_pos)))
  {
    return false;
  }
  auto &inv = controlled->GetInventory();
  const InventoryEntryRef *active = inv.GetActiveEntryRef();
  if (active && !active->empty && active->kind == InventoryEntryKind::Item)
  {
    auto &bars = inv.GetHotbarsMutable();
    const size_t bar = inv.GetActiveBarIndex();
    const size_t slot = inv.GetActiveSlotIndex();
    if (bar < bars.size() && slot < bars[bar].slots.size() &&
        !bars[bar].slots[slot].empty)
    {
      auto &entry = bars[bar].slots[slot].entry;
      if (entry.count < 0)
      {
        // Unlimited creative stack stays in hand and in storage.
      }
      else
      {
        auto &storage = inv.GetStorageMutable();
        auto sit = storage.find(active->Id);
        if (sit != storage.end() && sit->second > 0)
        {
          sit->second -= 1;
          if (sit->second == 0)
          {
            storage.erase(sit);
          }
        }
        if (entry.count > 1)
        {
          entry.count -= 1;
        }
        else
        {
          bars[bar].slots[slot].empty = true;
          bars[bar].slots[slot].entry = InventoryEntryRef{};
        }
      }
    }
  }
  world.UpdateIntersection(position, front);
  return true;
}

bool UBlockPlacementService::PlaceActiveObjectByView(
    UWorld &world, const glm::vec3 &position, const glm::vec3 &front)
{
  auto user = world.GetCurrentUser();
  if (!user)
  {
    return false;
  }

  UCreature *controlled = world.GetControlledCreature();
  if (!controlled)
  {
    return false;
  }
  const std::string &prefabName =
      controlled->GetInventory().GetActiveObjectName();
  if (prefabName.empty())
  {
    return false;
  }

  const auto anchor = world.FindObjectAnchorFromView(position, front);
  if (!anchor.has_value())
  {
    return false;
  }
  if (world.PlaceObject(prefabName, anchor.value()))
  {
    world.UpdateIntersection(position, front);
    return true;
  }
  return false;
}

} // namespace cutum
