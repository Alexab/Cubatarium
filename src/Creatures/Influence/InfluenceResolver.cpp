#include "Creatures/Influence/InfluenceResolver.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Influence/BareHandToolInfluenceProvider.h"
#include "Creatures/Influence/InfluenceHitMath.h"
#include "Game/Inventory/InventoryTypes.h"
#include "Game/ModePolicy.h"
#include "Items/ItemDefinitionStorage.h"
#include "Items/ItemUseRegistry.h"
#include "Items/ToolCapabilities.h"
#include "World/Core/World.h"
#include <algorithm>
#include <cmath>

namespace cutum
{

namespace
{
uint64_t ResolveTargetId(const CreatureIntent &intent)
{
  return intent.Influence.TargetId;
}

InfluenceChannel ResolveChannel(const CreatureIntent &intent)
{
  return intent.Influence.Channel;
}

float HorizontalDistance(const glm::vec3 &a, const glm::vec3 &b)
{
  const float dx = a.x - b.x;
  const float dz = a.z - b.z;
  return std::sqrt(dx * dx + dz * dz);
}
} // namespace

InfluencePrediction InfluenceResolver::Resolve(
    UWorld &world, UCreature &source, WorldGameMode mode,
    const IUToolInfluenceProvider *tools)
{
  InfluencePrediction pred;
  pred.SourceId = source.GetId();
  pred.SourcePos = source.GetBodyOrigin();

  const CreatureIntent &intent = source.GetIntent();
  const InfluenceChannel channel = ResolveChannel(intent);
  if (channel == InfluenceChannel::None)
  {
    return pred;
  }

  if (channel == InfluenceChannel::Use)
  {
    pred.Capability.Channel = channel;
    const UItemDefinitionStorage *items = world.GetItemDefinitionStorage();
    const InventoryEntryRef *active = source.GetInventory().GetActiveEntryRef();
    if (!active || active->empty || active->kind != InventoryEntryKind::Item ||
        active->Id.empty() || !items)
    {
      pred.CancelReason = "no_use_item";
      pred.Cancelled = true;
      return pred;
    }
    const ItemDefinition *def = items->Get(active->Id);
    if (!def)
    {
      pred.CancelReason = "unknown_item";
      pred.Cancelled = true;
      return pred;
    }
    const ItemUseParams use = ItemUseRegistry::FromDefinition(*def);
    if (use.Action != ItemUseAction::Eat && use.Action != ItemUseAction::Drink)
    {
      pred.CancelReason = "no_use_action";
      pred.Cancelled = true;
      return pred;
    }
    if (source.GetTimeSinceLastInfluenceSec() < 0.35f)
    {
      pred.CancelReason = "cooldown";
      pred.Cancelled = true;
      return pred;
    }
    pred.UseSelf = true;
    pred.UseItemId = active->Id;
    pred.UseSatietyDelta = use.SatietyDelta;
    pred.UseThirstDelta = use.ThirstDelta;
    pred.UseHealthDelta = use.HealthDelta;
    pred.Capability.Id = active->Id;
    pred.Capability.FullIntervalSec = 0.35f;
    InfluenceTargetDelta self;
    self.TargetId = source.GetId();
    self.TargetPos = source.GetBodyOrigin();
    self.HealthDelta = use.HealthDelta;
    pred.Targets.push_back(self);
    pred.Valid = true;
    return pred;
  }

  if (channel != InfluenceChannel::Dig &&
      !ModePolicy::AllowsCombatDamage(mode))
  {
    pred.CancelReason = "creative";
    pred.Cancelled = true;
    return pred;
  }

  if (channel != InfluenceChannel::Dig &&
      !ModePolicy::AllowsHostileAggro(mode, world.GetDifficulty()))
  {
    // Safety net: hostile melee vs player cancelled on Peaceful.
    const CreatureIntent &early = source.GetIntent();
    const uint64_t early_target = ResolveTargetId(early);
    if (early_target != 0)
    {
      if (const UCreature *t = world.GetCreature(early_target))
      {
        if (t->IsPlayerCharacter() && !source.IsPlayerCharacter())
        {
          pred.CancelReason = "peaceful";
          pred.Cancelled = true;
          return pred;
        }
      }
    }
  }

  if (channel == InfluenceChannel::Dig)
  {
    pred.Capability.Channel = channel;
    if (!intent.Influence.HasTargetBlock)
    {
      pred.CancelReason = "no_block_target";
      pred.Cancelled = true;
      return pred;
    }

    pred.DigBlockPos = intent.Influence.TargetBlockPos;
    const BlockId block_id =
        world.GetBlockWorld().GetBlock(pred.DigBlockPos);
    const UBlockDefinitionStorage *blocks =
        world.GetBlockRegistry().GetDefinitions();
    const BlockDefinition *block = blocks ? blocks->GetById(block_id) : nullptr;
    if (!block)
    {
      pred.CancelReason = "missing_block";
      pred.Cancelled = true;
      return pred;
    }

    const UItemDefinitionStorage *items = world.GetItemDefinitionStorage();
    const ItemDefinition *tool = nullptr;
    if (const InventoryEntryRef *active =
            source.GetInventory().GetActiveEntryRef())
    {
      if (!active->empty && active->kind == InventoryEntryKind::Item &&
          !active->broken && items)
      {
        tool = items->Get(active->Id);
      }
    }
    if (!tool && items)
    {
      tool = items->GetHandDefinition();
    }

    const DigParams dig =
        ResolveDigParams(tool, *block, source.GetAttributes(), mode);
    pred.Capability.Id = tool ? tool->Id : "bare_hand";
    pred.DigDurationSec = dig.DurationSec;
    pred.DigWearDelta = dig.WearDelta;
    pred.DigEffective = dig.Effective;
    pred.DigToolId = tool ? tool->Id : std::string{};
    if (!dig.Effective)
    {
      pred.CancelReason = "ineffective_dig";
      pred.Cancelled = true;
      return pred;
    }
    pred.Valid = true;
    return pred;
  }

  InfluenceCapability cap;
  const UBareHandToolInfluenceProvider bare;
  const IUToolInfluenceProvider *provider = tools ? tools : &bare;
  if (!provider->TryGetCapability(source, channel, cap))
  {
    if (channel == InfluenceChannel::Ranged ||
        !bare.TryGetCapability(source, InfluenceChannel::Melee, cap))
    {
      pred.CancelReason = "no_capability";
      pred.Cancelled = true;
      return pred;
    }
  }
  pred.Capability = cap;

  if (channel == InfluenceChannel::Ranged)
  {
    const UItemDefinitionStorage *items = world.GetItemDefinitionStorage();
    if (items)
    {
      if (const ItemDefinition *bow = items->Get(cap.Id))
      {
        if (!bow->Ranged.AmmoId.empty())
        {
          const auto &storage = source.GetInventory().GetStorage();
          const auto it = storage.find(bow->Ranged.AmmoId);
          bool haveAmmo = it != storage.end() && it->second != 0;
          if (!haveAmmo)
          {
            for (const auto &bar : source.GetInventory().GetHotbars())
            {
              for (const auto &slot : bar.slots)
              {
                if (!slot.entry.empty &&
                    slot.entry.kind == InventoryEntryKind::Item &&
                    slot.entry.Id == bow->Ranged.AmmoId &&
                    slot.entry.count != 0)
                {
                  haveAmmo = true;
                  break;
                }
              }
              if (haveAmmo)
              {
                break;
              }
            }
          }
          if (!haveAmmo)
          {
            pred.CancelReason = "no_ammo";
            pred.Cancelled = true;
            return pred;
          }
        }
      }
    }
  }

  if (source.GetTimeSinceLastInfluenceSec() < cap.FullIntervalSec)
  {
    pred.CancelReason = "cooldown";
    pred.Cancelled = true;
    return pred;
  }

  // Aura / radius prototype: hit all neighbors in RadiusBlocks.
  if (cap.Targeting == InfluenceTargeting::Radius && cap.RadiusBlocks > 0.f)
  {
    const auto neighbors = world.QueryCreatureNeighborsInRadius(
        source.GetBodyOrigin(), cap.RadiusBlocks, source.GetId());
    for (const auto &n : neighbors)
    {
      UCreature *target = world.GetCreature(n.Id);
      if (!target)
      {
        continue;
      }
      const InfluenceHitParams hit = InfluenceHitMath::Compute(
          target->GetArmorGroups(), cap, source.GetAttributes(),
          source.GetTimeSinceLastInfluenceSec());
      if (hit.Missed || !hit.DidHit || hit.Damage <= 0.f)
      {
        continue;
      }
      InfluenceTargetDelta delta;
      delta.TargetId = n.Id;
      delta.TargetPos = target->GetBodyOrigin();
      delta.HealthDelta = -hit.Damage;
      pred.Targets.push_back(delta);
      pred.IntervalMul = hit.IntervalMul;
    }
    if (pred.Targets.empty())
    {
      pred.CancelReason = "no_radius_targets";
      pred.Cancelled = true;
      return pred;
    }
    pred.SourceFatigueDelta = cap.SourceFatigueCost;
    pred.Valid = true;
    return pred;
  }

  const uint64_t target_id = ResolveTargetId(intent);
  if (target_id == 0 || target_id == source.GetId())
  {
    pred.CancelReason = "no_target";
    pred.Cancelled = true;
    return pred;
  }

  UCreature *target = world.GetCreature(target_id);
  if (!target)
  {
    pred.CancelReason = "missing_target";
    pred.Cancelled = true;
    return pred;
  }

  const float dist =
      HorizontalDistance(source.GetBodyOrigin(), target->GetBodyOrigin());
  const float reach = cap.RangeBlocks + 0.35f;
  if (dist > reach)
  {
    pred.CancelReason = "out_of_range";
    pred.Cancelled = true;
    return pred;
  }

  const InfluenceHitParams hit = InfluenceHitMath::Compute(
      target->GetArmorGroups(), cap, source.GetAttributes(),
      source.GetTimeSinceLastInfluenceSec());
  if (hit.Missed)
  {
    pred.CancelReason = "miss";
    pred.Cancelled = true;
    return pred;
  }
  if (!hit.DidHit || hit.Damage <= 0.f)
  {
    pred.CancelReason = hit.DidHit ? "zero_damage" : "no_hit";
    pred.Cancelled = true;
    return pred;
  }

  InfluenceTargetDelta delta;
  delta.TargetId = target_id;
  delta.TargetPos = target->GetBodyOrigin();
  delta.HealthDelta = -hit.Damage;
  pred.Targets.push_back(delta);
  // Status from capability / catalog (not hardcoded bleed).
  for (const std::string &status_id : cap.OnHitStatusIds)
  {
    if (!status_id.empty())
    {
      pred.Targets.back().StatusIdsToAdd.push_back(status_id);
    }
  }
  pred.SourceFatigueDelta = cap.SourceFatigueCost;
  pred.IntervalMul = hit.IntervalMul;
  pred.Valid = true;
  return pred;
}

} // namespace cutum
