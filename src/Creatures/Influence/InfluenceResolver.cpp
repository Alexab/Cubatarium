#include "Creatures/Influence/InfluenceResolver.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Influence/BareHandToolInfluenceProvider.h"
#include "Creatures/Influence/InfluenceHitMath.h"
#include "World/Core/World.h"
#include <algorithm>
#include <cmath>

namespace cutum
{

namespace
{
uint64_t ResolveTargetId(const CreatureIntent &intent)
{
  if (intent.Influence.TargetId != 0)
  {
    return intent.Influence.TargetId;
  }
  return intent.attackTargetId;
}

InfluenceChannel ResolveChannel(const CreatureIntent &intent)
{
  if (intent.Influence.Channel != InfluenceChannel::None)
  {
    return intent.Influence.Channel;
  }
  if (intent.attackTargetId != 0 || intent.Influence.TargetId != 0)
  {
    return InfluenceChannel::Melee;
  }
  return InfluenceChannel::None;
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

  if (mode == WorldGameMode::Creative)
  {
    pred.CancelReason = "creative";
    pred.Cancelled = true;
    return pred;
  }

  const CreatureIntent &intent = source.GetIntent();
  const InfluenceChannel channel = ResolveChannel(intent);
  if (channel == InfluenceChannel::None)
  {
    return pred;
  }

  InfluenceCapability cap;
  const UBareHandToolInfluenceProvider bare;
  const IUToolInfluenceProvider *provider = tools ? tools : &bare;
  if (!provider->TryGetCapability(source, channel, cap))
  {
    if (!bare.TryGetCapability(source, InfluenceChannel::Melee, cap))
    {
      pred.CancelReason = "no_capability";
      pred.Cancelled = true;
      return pred;
    }
  }
  pred.Capability = cap;

  if (source.GetTimeSinceLastInfluenceSec() < cap.FullIntervalSec)
  {
    pred.CancelReason = "cooldown";
    pred.Cancelled = true;
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
      target->GetArmorGroups(), cap, source.GetTimeSinceLastInfluenceSec());
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
  pred.SourceFatigueDelta = cap.SourceFatigueCost;
  pred.IntervalMul = hit.IntervalMul;
  pred.Valid = true;
  return pred;
}

} // namespace cutum
