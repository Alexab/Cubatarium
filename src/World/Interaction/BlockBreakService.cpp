#include "World/Interaction/BlockBreakService.h"

#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureInventory.h"
#include "Game/Economy/ResourceEconomy.h"
#include "Game/WorldGameMode.h"
#include "Items/ItemDefinitionStorage.h"
#include "Items/ToolCapabilities.h"
#include "World/Core/World.h"
#include "World/Math/BlockTypes.h"

namespace cutum
{

void UBlockBreakService::Start(glm::ivec3 blockPos, float pendingWearDelta,
                               std::string pendingToolId)
{
  DigSessionState session;
  session.Start(blockPos);
  session.pendingWearDelta = pendingWearDelta;
  session.pendingToolId = std::move(pendingToolId);
  Session = session;
}

void UBlockBreakService::Cancel()
{
  if (Session)
  {
    Session->Cancel();
  }
  Session.reset();
}

void UBlockBreakService::Tick(float dt, float durationSeconds)
{
  if (!Session)
  {
    return;
  }
  Session->Tick(dt, durationSeconds);
}

bool UBlockBreakService::Complete(UWorld &world)
{
  if (!Session)
  {
    return false;
  }
  const glm::ivec3 pos = Session->blockPos;
  const float wear_delta = Session->pendingWearDelta;
  const std::string tool_id = Session->pendingToolId;
  Session.reset();

  world.GetPhysicsTelemetryMutable().BreakCompleteN++;

  const BlockId removedId = world.GetBlockWorld().GetBlock(pos);
  const std::string blockTypeName =
      (removedId != BLOCK_AIR)
          ? world.GetBlockRegistry().GetTypeNameById(removedId)
          : std::string{};

  if (wear_delta > 0.f && !tool_id.empty())
  {
    if (UItemDefinitionStorage *items = world.GetItemDefinitionStorage())
    {
      if (UCreature *creature = world.GetControlledCreature())
      {
        const bool wear_enabled =
            IsToolWearEnabled(world.GetGameMode(), world.GetDifficulty());
        auto &bars = creature->GetInventory().GetHotbarsMutable();
        const size_t bar = creature->GetInventory().GetActiveBarIndex();
        const size_t slot = creature->GetInventory().GetActiveSlotIndex();
        if (bar < bars.size() && slot < bars[bar].slots.size())
        {
          auto &entry = bars[bar].slots[slot].entry;
          if (!bars[bar].slots[slot].empty &&
              entry.kind == InventoryEntryKind::Item && entry.Id == tool_id)
          {
            if (const ItemDefinition *def = items->Get(tool_id))
            {
              if (ApplyItemWear(entry, *def, wear_delta, wear_enabled))
              {
                bars[bar].slots[slot].empty = true;
                bars[bar].slots[slot].entry = InventoryEntryRef{};
              }
            }
          }
        }
      }
    }
  }

  if (!blockTypeName.empty())
  {
    if (UCreature *creature = world.GetControlledCreature())
    {
      ResourceEconomyService::GrantBlockDrop(world.GetGameMode(),
                                             creature->GetInventory(),
                                             blockTypeName,
                                             /*count=*/1);
    }
  }
  return world.DelBlockAt(pos);
}

float UBlockBreakService::GetProgress() const
{
  return Session ? Session->progress : 0.f;
}

std::optional<glm::ivec3> UBlockBreakService::GetBlockPos() const
{
  if (!Session)
  {
    return std::nullopt;
  }
  return Session->blockPos;
}

} // namespace cutum
