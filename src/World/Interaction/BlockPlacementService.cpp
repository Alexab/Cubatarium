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
