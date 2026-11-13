#include "World/Items/WorldItemSystem.h"

#include "Audio/AudioApi.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureInventory.h"
#include "Game/Economy/ResourceEconomy.h"
#include "Game/Inventory/InventoryTypes.h"
#include "Game/ModePolicy.h"
#include "Render/Engine/GeometryEngine.h"
#include "World/Core/World.h"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace cutum
{

namespace
{
constexpr float kPickupRadius = 1.75f;
constexpr float kTtlSeconds = 180.f;
constexpr std::size_t kMaxWorldItems = 512;
constexpr float kBobAmplitude = 0.04f;
}

WorldItemSystem &WorldItemSystem::Instance()
{
  static WorldItemSystem system;
  return system;
}

int WorldItemSystem::Spawn(const glm::vec3 &position, const std::string &itemId,
                           int count, bool isBlock)
{
  if (itemId.empty() || count <= 0)
  {
    return 0;
  }
  WorldItem item;
  item.id = NextId++;
  item.itemId = itemId;
  item.count = count;
  item.isBlock = isBlock;
  item.position = position + glm::vec3(0.f, 0.35f, 0.f);
  item.ttl = kTtlSeconds;
  if (Items.size() >= kMaxWorldItems)
  {
    return 0; // backpressure; never evict another player-owned payload
  }
  Items.push_back(std::move(item));
  return Items.back().id;
}

void WorldItemSystem::Tick(UWorld &world, float dt)
{
  if (!ModePolicy::DropsBlocksOnBreak(world.GetGameMode()))
  {
    Items.clear();
    return;
  }
  UCreature *player = world.GetControlledCreature();
  const glm::vec3 playerPos =
      player ? player->GetBodyOrigin() : glm::vec3(1e9f);
  for (auto it = Items.begin(); it != Items.end();)
  {
    it->age += dt;
    if (it->age >= it->ttl)
    {
      it = Items.erase(it);
      continue;
    }
    if (player && glm::distance(playerPos, it->position) <= kPickupRadius)
    {
      auto &inv = player->GetInventory();
      inv.SetItemDefinitions(world.GetItemDefinitionStorage());
      InventoryEntryRef pickup;
      pickup.empty = false; pickup.Id = it->itemId; pickup.count = it->count;
      pickup.kind = it->isBlock ? InventoryEntryKind::Block : InventoryEntryKind::Item;
      const auto accepted = inv.Insert(pickup);
      if (accepted.transferred == 0) { ++it; continue; }
      it->count = accepted.remainder;
      UAudioEvent audio;
      audio.Kind = AudioEventKind::Pickup;
      audio.X = playerPos.x;
      audio.Y = playerPos.y;
      audio.Z = playerPos.z;
      audio.EntityId = static_cast<std::uint32_t>(player->GetId());
      UAudioApi::Post(audio);
      if (it->count == 0) it = Items.erase(it);
      else ++it;
      continue;
    }
    ++it;
  }
}

void WorldItemSystem::Draw(UWorld &world, UGeometryEngine &engine,
                           const glm::mat4 &viewProj)
{
  (void)world;
  for (const WorldItem &item : Items)
  {
    // Bob is a render-time offset only: game state keeps the true spawn point.
    const glm::vec3 renderPos =
        item.position + glm::vec3(0.f, kBobAmplitude * std::sin(item.age * 3.f), 0.f);
    glm::mat4 model = glm::translate(glm::mat4(1.f), renderPos);
    model = glm::rotate(model, item.age * 0.8f, glm::vec3(0.f, 1.f, 0.f));
    const float s = item.isBlock ? 0.35f : 0.28f;
    model = glm::scale(model, glm::vec3(s));
    const glm::vec4 color = item.isBlock ? glm::vec4(0.55f, 0.75f, 0.4f, 1.f)
                                         : glm::vec4(0.95f, 0.72f, 0.28f, 1.f);
    engine.DrawBoxWireframe(viewProj * model, color);
  }
}

void WorldItemSystem::Clear() { Items.clear(); }

} // namespace cutum
