#include "Creatures/Influence/InfluenceApplier.h"
#include "Audio/AudioApi.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include "Creatures/Influence/InfluenceEvent.h"
#include "Creatures/Influence/StatusEffectSystem.h"
#include "Creatures/Stats/CreatureVitalsSystem.h"
#include "Game/Economy/ResourceEconomy.h"
#include "Game/Inventory/InventoryTypes.h"
#include "Game/ModePolicy.h"
#include "Items/ItemDefinitionStorage.h"
#include "Items/ToolCapabilities.h"
#include "World/Core/World.h"
#include "World/Items/WorldItemSystem.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <random>

namespace cutum
{

namespace
{

/// World sim is single-threaded; deterministic-per-tick picks (tick % size)
/// made armor wear cycle the same slot (audit M9).
std::mt19937 &ApplierRng()
{
  static std::mt19937 rng{static_cast<uint32_t>(0xC0BA7A5EULL)};
  return rng;
}

// Survival combat: wear a random equipped armor piece on a damaging hit.
void ApplyArmorWearForHit(UWorld &world, UCreature &target, WorldGameMode mode,
                          float amount)
{
  if (ModePolicy::AllowsCombatDamage(mode) &&
      ModePolicy::AllowsToolWear(mode, world.GetDifficulty()))
  {
    auto &inv = target.GetInventory();
    const UItemDefinitionStorage *items = world.GetItemDefinitionStorage();
    if (items)
    {
      std::vector<size_t> worn;
      for (size_t slot = 0; slot < UCreatureInventory::kArmorSlotCount; ++slot)
      {
        const InventoryEntryRef &e = inv.GetEquippedArmor(slot);
        if (!e.empty && !e.broken && e.kind == InventoryEntryKind::Item)
        {
          worn.push_back(slot);
        }
      }
      if (!worn.empty())
      {
        const size_t pick = worn[std::uniform_int_distribution<size_t>{
            0, worn.size() - 1}(ApplierRng())];
        InventoryEntryRef armor = inv.GetEquippedArmor(pick);
        if (const ItemDefinition *def = items->Get(armor.Id))
        {
          const float armorWear = 0.01f * amount;
          inv.WearArmor(pick, armorWear, *items);
        }
      }
    }
  }
}

// Active frontal block wears offhand even when damage mul zeroes the hit.
void ApplyShieldWearForBlock(UWorld &world, UCreature &target,
                             WorldGameMode mode)
{
  if (!(ModePolicy::AllowsCombatDamage(mode) &&
        ModePolicy::AllowsToolWear(mode, world.GetDifficulty())))
  {
    return;
  }
  auto &inv = target.GetInventory();
  const UItemDefinitionStorage *items = world.GetItemDefinitionStorage();
  if (!items)
  {
    return;
  }
  InventoryEntryRef off = inv.GetEquippedOffhand();
  if (off.empty || off.broken || off.kind != InventoryEntryKind::Item)
  {
    return;
  }
  const ItemDefinition *shield = items->Get(off.Id);
  if (!shield || !shield->Block.Enabled)
  {
    return;
  }
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
  inv.WearOffhand(shieldWear, *items);
}

/// Consume one arrow from storage, then hotbar slots (negative storage
/// counts are unlimited creative stock).
void ConsumeRangedAmmo(UWorld &world, UCreature &source,
                       const std::string &capabilityId)
{
  const UItemDefinitionStorage *items = world.GetItemDefinitionStorage();
  if (!items)
  {
    return;
  }
  const ItemDefinition *bow = items->Get(capabilityId);
  if (!bow || bow->Ranged.AmmoId.empty())
  {
    return;
  }
  (void)ResourceEconomyService::ConsumeUnits(
      world.GetGameMode(), source.GetInventory(), InventoryEntryKind::Item,
      bow->Ranged.AmmoId, 1);
}

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

void InfluenceApplier::ApplyImpact(UWorld &world, uint64_t id, uint64_t thrower,
                                   float damage, glm::vec3 hitFrom,
                                   std::optional<InventoryDomain> domain)
{
  UWorldItemSystem::DamageScope damageScope(domain);
  auto *target = world.GetCreature(id);
  if (!target || damage <= 0.f)
    return;
  const auto armor = target->GetArmorGroups();
  if (armor.IsImmortal())
    return;
  damage *= std::clamp(armor.Get("fleshy") / 100.f, 0.f, 2.f);
  const auto &off = target->GetInventory().GetEquippedOffhand();
  const auto *items = world.GetItemDefinitionStorage();
  const auto *shield = !off.empty && items ? items->Get(off.Id) : nullptr;
  if (target->CarryHandCount == 0 && target->BowCharge <= 0 && shield &&
      shield->Block.Enabled && !off.broken)
  {
    auto direction = hitFrom - target->GetBodyOrigin();
    direction.y = 0;
    const auto forward = glm::vec3(std::sin(glm::radians(target->GetYaw())), 0,
                                   std::cos(glm::radians(target->GetYaw())));
    bool frontal = glm::length(direction) > .001f &&
                   glm::dot(glm::normalize(direction), forward) >=
                       std::cos(glm::radians(shield->Block.AngleDeg * .5f));
    if (frontal && target->IsBlocking())
    {
      damage *= shield->Block.DamageMul;
      ApplyShieldWearForBlock(world, *target, world.GetGameMode());
    }
    else
      damage *= shield->Block.PassiveDamageMul;
  }
  const auto position = target->GetBodyOrigin();
  InfluenceEvent impact;
  impact.Kind = InfluenceEventKind::Applied;
  impact.SourceId = thrower;
  impact.CapabilityId = "physical_impact";
  impact.Channel = InfluenceChannel::Melee;
  impact.SourcePos = hitFrom;
  impact.TargetPos = position;
  impact.TargetIds.push_back(id);
  impact.DamageDealt = damage;
  impact.Effects.Target.FlashStrength = .6f;
  impact.Effects.Target.DurationSec = .2f;
  InfluenceEvents::Emit(impact);
  if (CreatureVitalsSystem::ApplyDamage(
          world, *target, damage, world.GetGameMode(), "influence", thrower))
  {
    world.RemoveCreature(id);
    return;
  }
  ApplyArmorWearForHit(world, *target, world.GetGameMode(), damage);
}

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
      // Dig hit on the block surface, synced to the progress tick.
      UAudioEvent audio;
      audio.Kind = AudioEventKind::DigHit;
      audio.X = progress_ev.TargetPos.x + 0.5f;
      audio.Y = progress_ev.TargetPos.y + 0.5f;
      audio.Z = progress_ev.TargetPos.z + 0.5f;
      audio.MaterialId = world.ResolveAudioMaterialAt(pred.DigBlockPos);
      audio.EntityId = static_cast<std::uint32_t>(pred.SourceId);
      UAudioApi::Post(audio);
      result.Applied = true;
      return result;
    }

    // Resolve the block material while the block is still in place; the
    // completed-break thud must name the material that just disappeared.
    const std::uint32_t digMaterialId =
        world.ResolveAudioMaterialAt(pred.DigBlockPos);
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
    if (result.Applied)
    {
      UAudioEvent audio;
      audio.Kind = AudioEventKind::DigComplete;
      audio.X = ev.TargetPos.x + 0.5f;
      audio.Y = ev.TargetPos.y + 0.5f;
      audio.Z = ev.TargetPos.z + 0.5f;
      audio.MaterialId = digMaterialId;
      audio.EntityId = static_cast<std::uint32_t>(pred.SourceId);
      UAudioApi::Post(audio);
    }
    return result;
  }

  if (pred.Capability.Channel == InfluenceChannel::Use && pred.UseSelf)
  {
    if (const auto *defs = world.GetItemDefinitionStorage())
      if (const auto *def = defs->Get(pred.UseItemId);
          def && def->Use.Interaction == "container" &&
          !source->GetInventory().IsCreative())
      {
        auto *active = source->GetInventory().GetActiveEntryRef();
        if (!active || active->Id != pred.UseItemId ||
            active->contentsId != "water" || active->contentsUnits <= 0)
          return result;
        if (--active->contentsUnits == 0)
          active->contentsId.clear();
      }
    if (pred.UseConsumes && !ResourceEconomyService::ConsumeUnits(
                                mode, source->GetInventory(),
                                InventoryEntryKind::Item, pred.UseItemId, 1))
      return result;
    CreatureVitals &v = source->GetVitals();
    if (pred.UseSatietyDelta != 0.f)
    {
      v.satiety =
          std::clamp(v.satiety + pred.UseSatietyDelta, 0.f, v.maxSatiety);
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
    // Eat/drink feedback right at the consume moment.
    UAudioEvent audio;
    audio.Kind = AudioEventKind::Eat;
    audio.X = pred.SourcePos.x;
    audio.Y = pred.SourcePos.y;
    audio.Z = pred.SourcePos.z;
    audio.EntityId = static_cast<std::uint32_t>(pred.SourceId);
    UAudioApi::Post(audio);
    result.Applied = true;
    return result;
  }

  if (pred.SourceFatigueDelta > 0.f)
  {
    CreatureVitals &av = source->GetVitals();
    const float endMul =
        1.f / std::max(0.5f, 0.5f + static_cast<float>(
                                        source->GetAttributes().endurance) /
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
                                            "influence", pred.SourceId))
      {
        result.AnyTargetRemoved = true;
        world.RemoveCreature(delta.TargetId);
        continue;
      }

      ApplyArmorWearForHit(world, *target, mode, amount);
    }
    if (delta.ShieldBlocked)
    {
      ApplyShieldWearForBlock(world, *target, mode);
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
      ConsumeRangedAmmo(world, *source, pred.Capability.Id);
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
  // Combat feedback: the bow reports at the shooter, the hit lands on the
  // target only when damage was actually dealt.
  if (pred.Capability.Channel == InfluenceChannel::Ranged)
  {
    UAudioEvent audio;
    audio.Kind = AudioEventKind::RangedShot;
    audio.X = pred.SourcePos.x;
    audio.Y = pred.SourcePos.y;
    audio.Z = pred.SourcePos.z;
    audio.EntityId = static_cast<std::uint32_t>(pred.SourceId);
    UAudioApi::Post(audio);
  }
  if (total_damage > 0.f && pred.Capability.Channel == InfluenceChannel::Melee)
  {
    UAudioEvent audio;
    audio.Kind = AudioEventKind::MeleeHit;
    audio.X = primary_target_pos.x;
    audio.Y = primary_target_pos.y;
    audio.Z = primary_target_pos.z;
    audio.EntityId = static_cast<std::uint32_t>(pred.SourceId);
    UAudioApi::Post(audio);
  }
  result.Applied = true;
  return result;
}

} // namespace cutum
