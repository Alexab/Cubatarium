#include "Creatures/Influence/InfluenceApplier.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Influence/InfluenceEvent.h"
#include "Creatures/Influence/StatusEffectSystem.h"
#include "Creatures/Stats/CreatureVitalsSystem.h"
#include "Game/Inventory/InventoryTypes.h"
#include "Game/ModePolicy.h"
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
    const float dig_progress = world.GetBreakProgress();
    if (dig_progress < 1.f)
    {
      InfluenceEvent progress_ev;
      progress_ev.Kind = InfluenceEventKind::DigProgress;
      progress_ev.SourceId = pred.SourceId;
      progress_ev.CapabilityId = pred.Capability.Id;
      progress_ev.Channel = InfluenceChannel::Dig;
      progress_ev.DigProgress = dig_progress;
      progress_ev.DigBlockPos = pred.DigBlockPos;
      progress_ev.SourcePos = pred.SourcePos;
      progress_ev.TargetPos = glm::vec3(pred.DigBlockPos);
      InfluenceEvents::Emit(progress_ev);
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
    ev.DigProgress = 1.f;
    ev.DigBlockPos = pred.DigBlockPos;
    ev.SourcePos = pred.SourcePos;
    ev.TargetPos = glm::vec3(pred.DigBlockPos);
    if (!result.Applied)
    {
      ev.CancelReason = "dig_complete_failed";
    }
    InfluenceEvents::Emit(ev);
    return result;
  }

  if (pred.Capability.Channel == InfluenceChannel::Use && pred.UseSelf)
  {
    CreatureVitals &v = source->GetVitals();
    if (pred.UseSatietyDelta != 0.f)
    {
      v.satiety = std::clamp(v.satiety + pred.UseSatietyDelta, 0.f, v.maxSatiety);
    }
    if (pred.UseThirstDelta != 0.f)
    {
      v.thirst = std::clamp(v.thirst + pred.UseThirstDelta, 0.f, v.maxThirst);
    }
    if (pred.UseHealthDelta != 0.f)
    {
      v.health = std::clamp(v.health + pred.UseHealthDelta, 0.f, v.maxHealth);
    }
    v.ClampCurrents();

    // Consume one from storage and active hotbar if present.
    auto &storage = source->GetInventory().GetStorageMutable();
    const auto it = storage.find(pred.UseItemId);
    if (it != storage.end() && it->second > 0)
    {
      --it->second;
      if (it->second <= 0)
      {
        storage.erase(it);
      }
    }
    if (InventoryEntryRef *active = source->GetInventory().GetActiveEntryRef())
    {
      if (!active->empty && active->kind == InventoryEntryKind::Item &&
          active->Id == pred.UseItemId)
      {
        if (active->count > 1)
        {
          --active->count;
        }
        else
        {
          const size_t bar = source->GetInventory().GetActiveBarIndex();
          const size_t slot = source->GetInventory().GetActiveSlotIndex();
          source->GetInventory().ClearHotbarSlot(bar, slot);
        }
      }
    }

    source->ResetInfluenceCooldown();
    InfluenceEvent ev;
    ev.Kind = InfluenceEventKind::Applied;
    ev.SourceId = pred.SourceId;
    ev.CapabilityId = pred.Capability.Id;
    ev.Channel = InfluenceChannel::Use;
    ev.SourcePos = pred.SourcePos;
    ev.TargetPos = pred.SourcePos;
    ev.Effects = pred.Capability.Effects;
    InfluenceEvents::Emit(ev);
    result.Applied = true;
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

      // Survival combat: wear a random equipped armor piece.
      if (ModePolicy::AllowsCombatDamage(mode) && ModePolicy::AllowsToolWear(
              mode, world.GetDifficulty()))
      {
        auto &inv = target->GetInventory();
        const UItemDefinitionStorage *items = world.GetItemDefinitionStorage();
        if (items)
        {
          std::vector<size_t> worn;
          for (size_t slot = 0; slot < 6; ++slot)
          {
            const InventoryEntryRef &e = inv.GetEquippedArmor(slot);
            if (!e.empty && !e.broken && e.kind == InventoryEntryKind::Item)
            {
              worn.push_back(slot);
            }
          }
          if (!worn.empty())
          {
            const size_t pick = worn[static_cast<size_t>(
                world.GetPhysicsTickCounter() % worn.size())];
            InventoryEntryRef armor = inv.GetEquippedArmor(pick);
            if (const ItemDefinition *def = items->Get(armor.Id))
            {
              const float armorWear = 0.01f * amount;
              if (ApplyItemWear(armor, *def, armorWear, true))
              {
                inv.UnequipArmor(pick, *items);
              }
              else
              {
                inv.EquipArmor(pick, armor, *items);
              }
            }
          }
        }
      }
    }
    // Active frontal block wears offhand even when damage mul zeroes the hit.
    if (delta.ShieldBlocked &&
        ModePolicy::AllowsCombatDamage(mode) &&
        ModePolicy::AllowsToolWear(mode, world.GetDifficulty()))
    {
      auto &inv = target->GetInventory();
      const UItemDefinitionStorage *items = world.GetItemDefinitionStorage();
      if (items)
      {
        InventoryEntryRef off = inv.GetEquippedOffhand();
        if (!off.empty && !off.broken && off.kind == InventoryEntryKind::Item)
        {
          if (const ItemDefinition *shield = items->Get(off.Id))
          {
            if (shield->Block.Enabled)
            {
              int uses = shield->Block.BlockUses;
              if (uses <= 0)
              {
                uses = shield->Tool.PunchAttackUses;
              }
              if (uses <= 0)
              {
                uses = 100;
              }
              const float shieldWear = 1.f / static_cast<float>(uses);
              if (ApplyItemWear(off, *shield, shieldWear, true))
              {
                inv.UnequipOffhand(*items);
              }
              else
              {
                inv.EquipOffhand(off, *items);
              }
            }
          }
        }
      }
    }
    for (const std::string &status_id : delta.StatusIdsToAdd)
    {
      StatusEffectSystem::ApplyStatus(*target, status_id);
    }
  }

  if ((pred.Capability.Channel == InfluenceChannel::Melee ||
       pred.Capability.Channel == InfluenceChannel::Ranged) &&
      total_damage > 0.f)
  {
    ApplyMeleeToolWear(world, *source, pred.Capability);
    if (pred.Capability.Channel == InfluenceChannel::Ranged)
    {
      const UItemDefinitionStorage *items = world.GetItemDefinitionStorage();
      if (items)
      {
        if (const ItemDefinition *bow = items->Get(pred.Capability.Id))
        {
          if (!bow->Ranged.AmmoId.empty())
          {
            auto &storage = source->GetInventory().GetStorageMutable();
            const auto it = storage.find(bow->Ranged.AmmoId);
            if (it != storage.end() && it->second < 0)
            {
              // Unlimited creative stock.
            }
            else if (it != storage.end() && it->second > 0)
            {
              --it->second;
              if (it->second <= 0)
              {
                storage.erase(it);
              }
            }
            else
            {
              auto &hotbars = source->GetInventory().GetHotbarsMutable();
              bool done = false;
              for (auto &bar : hotbars)
              {
                for (auto &slot : bar.slots)
                {
                  if (slot.entry.empty ||
                      slot.entry.kind != InventoryEntryKind::Item ||
                      slot.entry.Id != bow->Ranged.AmmoId)
                  {
                    continue;
                  }
                  if (slot.entry.count > 1)
                  {
                    --slot.entry.count;
                  }
                  else
                  {
                    slot.entry = InventoryEntryRef{};
                    slot.empty = true;
                  }
                  done = true;
                  break;
                }
                if (done)
                {
                  break;
                }
              }
            }
          }
        }
      }
    }
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
