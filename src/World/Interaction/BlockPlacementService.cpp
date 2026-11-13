#include "World/Interaction/BlockPlacementService.h"

#include "Audio/AudioApi.h"
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

namespace
{

/// Thud right after the block appears; material of the block itself.
void PostPlaceAudio(UWorld &world, const glm::ivec3 &blockPos)
{
  UAudioEvent audio;
  audio.Kind = AudioEventKind::Place;
  audio.X = static_cast<float>(blockPos.x) + 0.5f;
  audio.Y = static_cast<float>(blockPos.y) + 0.5f;
  audio.Z = static_cast<float>(blockPos.z) + 0.5f;
  audio.MaterialId = world.ResolveAudioMaterialAt(blockPos);
  UAudioApi::Post(audio);
}

} // namespace

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
      activeEntry->kind != InventoryEntryKind::Block || activeEntry->Id.empty())
  {
    return false;
  }
  const std::string &blockType = activeEntry->Id;
  const InventoryEntryRef entryCopy = *activeEntry;
  if (!ResourceEconomyService::CanPlace(world.GetGameMode(),
                                        controlled->GetInventory(), entryCopy))
  {
    return false;
  }

  PlayerCapsule cap = world.ViewBinding
                          ? world.ViewBinding->ResolvePlacementCapsule(world)
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
  auto trial = controlled->GetInventory();
  if (!ResourceEconomyService::ConsumeOnPlace(world.GetGameMode(), trial,
                                              entryCopy))
    return false;
  if (world.AddObject(blockType, BlockCenter(*resolved.place_block_pos)))
  {
    controlled->GetInventory() = std::move(trial);
    PostPlaceAudio(world, *resolved.place_block_pos);
    world.UpdateIntersection(position, front);
    return true;
  }
  return false;
}

bool UBlockPlacementService::PlaceBlockFromHeldItem(UWorld &world,
                                                    const glm::vec3 &position,
                                                    const glm::vec3 &front,
                                                    const std::string &blockId)
{
  UCreature *controlled = world.GetControlledCreature();
  if (!controlled || blockId.empty())
  {
    return false;
  }
  auto &inventory = controlled->GetInventory();
  const auto *source = inventory.GetActiveEntryRef();
  if (!source || source->empty || source->kind != InventoryEntryKind::Item ||
      controlled->CarryHandCount > 0)
    return false;
  auto trial = inventory;
  if (!ResourceEconomyService::ConsumeUnits(
          world.GetGameMode(), trial, InventoryEntryKind::Item, source->Id, 1))
    return false;
  PlayerCapsule cap = world.ViewBinding
                          ? world.ViewBinding->ResolvePlacementCapsule(world)
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
  inventory = std::move(trial);
  PostPlaceAudio(world, *resolved.place_block_pos);
  world.UpdateIntersection(position, front);
  return true;
}

bool UBlockPlacementService::PlaceActiveObjectByView(UWorld &world,
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
