#include "Creatures/Influence/InfluenceApplier.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Influence/InfluenceEvent.h"
#include "Creatures/Influence/StatusEffectSystem.h"
#include "Creatures/Stats/CreatureVitalsSystem.h"
#include "Game/Inventory/InventoryTypes.h"
#include "Items/ItemDefinitionStorage.h"
#include "Items/ToolCapabilities.h"
#include "World/Core/World.h"
#include <algorithm>
#include <cmath>
#include <optional>

namespace cutum
{

namespace
{

void ApplyMeleeToolWear(UWorld &world, UCreature &source,
                        const InfluenceCapability &cap)
{
  if (cap.PunchAttackUses <= 0)
  {
    return;
  }
  InventoryEntryRef *active = source.GetInventory().GetActiveEntryRef();
  if (!active || active->empty || active->kind != InventoryEntryKind::Item)
  {
    return;
  }
  const UItemDefinitionStorage *items = world.GetItemDefinitionStorage();
  if (!items)
  {
    return;
  }
  const ItemDefinition *def = items->Get(active->Id);
  if (!def || def->Id != cap.Id)
  {
    return;
  }
  const float wear_delta = 1.f / static_cast<float>(cap.PunchAttackUses);
  const bool wear_on =
      IsToolWearEnabled(world.GetGameMode(), world.GetDifficulty());
  if (ApplyItemWear(*active, *def, wear_delta, wear_on))
  {
    // Slot cleared (destroy).
  }
}

} // namespace

InfluenceApplyResult InfluenceApplier::Apply(UWorld &world,
                                             InfluencePrediction &pred,
                                             WorldGameMode mode, float dt)
{
  InfluenceApplyResult result;
  if (!pred.Valid || pred.Cancelled)
  {
    if (pred.Capability.Channel == InfluenceChannel::Dig)
    {
      world.CancelBreakSession();
    }
    InfluenceEvent ev;
    ev.Kind = InfluenceEventKind::Cancelled;
    ev.SourceId = pred.SourceId;
    ev.CapabilityId = pred.Capability.Id;
    ev.Channel = pred.Capability.Channel;
    ev.SourcePos = pred.SourcePos;
    ev.Effects = pred.Capability.Effects;
    ev.CancelReason = pred.CancelReason;
    for (const auto &t : pred.Targets)
    {
      ev.TargetIds.push_back(t.TargetId);
    }
    InfluenceEvents::Emit(ev);
    return result;
  }

  UCreature *source = world.GetCreature(pred.SourceId);
  if (!source)
  {
    return result;
  }

  if (pred.Capability.Channel == InfluenceChannel::Dig)
  {
    const std::optional<glm::ivec3> session_pos =
        world.GetBreakSessionBlockPos();
    if (!session_pos || *session_pos != pred.DigBlockPos)
    {
      world.StartBreakSession(pred.DigBlockPos, pred.DigWearDelta,
                              pred.DigToolId);
    }
    world.TickBreakSession(dt, pred.DigDurationSec);
    if (world.GetBreakProgress() < 1.f)
    {
      result.Applied = true;
      return result;
    }

    result.Applied = world.CompleteBreakSession();
    InfluenceEvent ev;
    ev.Kind = result.Applied ? InfluenceEventKind::Applied
                             : InfluenceEventKind::Cancelled;
    ev.SourceId = pred.SourceId;
    ev.CapabilityId = pred.Capability.Id;
    ev.Channel = InfluenceChannel::Dig;
    ev.SourcePos = pred.SourcePos;
    ev.TargetPos = glm::vec3(pred.DigBlockPos);
    if (!result.Applied)
    {
      ev.CancelReason = "dig_complete_failed";
    }
    InfluenceEvents::Emit(ev);
    return result;
  }

  if (pred.SourceFatigueDelta > 0.f)
  {
    CreatureVitals &av = source->GetVitals();
    const float endMul =
        1.f /
        std::max(0.5f,
                 0.5f + static_cast<float>(source->GetAttributes().endurance) /
                            20.f);
    av.fatigue =
        std::min(av.maxFatigue, av.fatigue + pred.SourceFatigueDelta * endMul);
    av.ClampCurrents();
  }

  float total_damage = 0.f;
  glm::vec3 primary_target_pos = pred.SourcePos;
  for (const InfluenceTargetDelta &delta : pred.Targets)
  {
    UCreature *target = world.GetCreature(delta.TargetId);
    if (!target)
    {
      continue;
    }
    primary_target_pos = delta.TargetPos;
    if (delta.HealthDelta < 0.f)
    {
      const float amount = -delta.HealthDelta;
      total_damage += amount;
      if (CreatureVitalsSystem::ApplyDamage(world, *target, amount, mode,
                                            "influence"))
      {
        result.AnyTargetRemoved = true;
        world.RemoveCreature(delta.TargetId);
        continue;
      }
    }
    for (const std::string &status_id : delta.StatusIdsToAdd)
    {
      StatusEffectSystem::ApplyStatus(*target, status_id);
    }
  }

  if (pred.Capability.Channel == InfluenceChannel::Melee &&
      total_damage > 0.f)
  {
    ApplyMeleeToolWear(world, *source, pred.Capability);
  }

  source->ResetInfluenceCooldown();

  InfluenceEvent ev;
  ev.Kind = InfluenceEventKind::Applied;
  ev.SourceId = pred.SourceId;
  ev.CapabilityId = pred.Capability.Id;
  ev.Channel = pred.Capability.Channel;
  ev.DamageDealt = total_damage;
  ev.SourcePos = pred.SourcePos;
  ev.TargetPos = primary_target_pos;
  ev.Effects = pred.Capability.Effects;
  for (const auto &t : pred.Targets)
  {
    ev.TargetIds.push_back(t.TargetId);
  }
  InfluenceEvents::Emit(ev);
  result.Applied = true;
  return result;
}

} // namespace cutum
