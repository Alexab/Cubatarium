#include "Creatures/Influence/InfluenceApplier.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Influence/InfluenceEvent.h"
#include "Creatures/Influence/StatusEffectSystem.h"
#include "Creatures/Stats/CreatureVitalsSystem.h"
#include "World/Core/World.h"
#include <algorithm>
#include <cmath>

namespace cutum
{

InfluenceApplyResult InfluenceApplier::Apply(UWorld &world,
                                             InfluencePrediction &pred,
                                             WorldGameMode mode)
{
  InfluenceApplyResult result;
  if (!pred.Valid || pred.Cancelled)
  {
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
