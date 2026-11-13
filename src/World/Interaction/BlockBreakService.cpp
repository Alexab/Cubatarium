#include "World/Interaction/BlockBreakService.h"
#include "Game/Survival/SurvivalSystems.h"

#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureInventory.h"
#include "Game/Economy/ResourceEconomy.h"
#include "Game/ModePolicy.h"
#include "Game/WorldGameMode.h"
#include "Items/ItemDefinitionStorage.h"
#include "Items/ToolCapabilities.h"
#include "World/Core/World.h"
#include "World/Items/WorldItemSystem.h"
#include "World/Math/BlockTypes.h"

namespace cutum
{

void UBlockBreakService::Start(glm::ivec3 blockPos, float pendingWearDelta,
                               std::string pendingToolId)
{
  SourceOwner = 0;
  SourceInstance = 0;
  SourceSlot = 0;
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

  const BlockId removedId = world.GetBlockWorld().GetBlock(pos);
  if (removedId == BLOCK_AIR)
    return false;
  const auto blockTypeName =
      world.GetBlockRegistry().GetTypeNameById(removedId);
  auto *actor = world.GetControlledCreature();
  if (SourceOwner && (!actor || actor->GetId() != SourceOwner))
    return false;
  std::optional<UCreatureInventory> trial;
  if (actor)
  {
    auto &inv = actor->GetInventory();
    const auto *active = inv.GetActiveEntryRef();
    if (SourceOwner &&
        (inv.GetActiveBarIndex() * 10 + inv.GetActiveSlotIndex() !=
             SourceSlot ||
         (active ? active->instanceId : 0) != SourceInstance))
      return false;
    trial = inv;
    if (wear_delta > 0 && !tool_id.empty())
    {
      auto *entry = trial->GetActiveEntryRef();
      auto *items = world.GetItemDefinitionStorage();
      auto *def = items ? items->Get(tool_id) : nullptr;
      if (!entry || entry->Id != tool_id || entry->broken || !def)
        return false;
      ApplyItemWear(
          *entry, *def, wear_delta,
          IsToolWearEnabled(world.GetGameMode(), world.GetDifficulty()));
    }
  }
  world.GetPhysicsTelemetryMutable().BreakCompleteN++;
  if (!world.DelBlockAt(pos))
  {
    world.GetPhysicsTelemetryMutable().BreakCompleteN--;
    return false;
  }
  if (actor && trial)
    actor->GetInventory() = std::move(*trial);
  if (!blockTypeName.empty() &&
      ModePolicy::DropsBlocksOnBreak(world.GetGameMode()))
    USurvivalSystems::Instance().BlockDrops(world, pos, blockTypeName);
  return true;
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
