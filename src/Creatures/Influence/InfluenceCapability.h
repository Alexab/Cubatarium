#ifndef INFLUENCE_CAPABILITY_H
#define INFLUENCE_CAPABILITY_H

#include "Creatures/Influence/EffectSpec.h"
#include "Creatures/Influence/InfluenceTypes.h"

namespace cutum
{

/// What an influence "weapon" / bare hand / future tool can do.
struct InfluenceCapability
{
  std::string Id{"bare_hand"};
  InfluenceChannel Channel{InfluenceChannel::Melee};
  InfluenceTargeting Targeting{InfluenceTargeting::Single};
  DamageGroups Damage;
  float FullIntervalSec{0.5f};
  float RangeBlocks{2.5f};
  float RadiusBlocks{0.f};
  float ConeDegrees{0.f};
  float SourceFatigueCost{6.f};
  int PunchAttackUses{0};
  EffectSpec Effects;

  static InfluenceCapability DefaultBareHand(int fleshy_damage)
  {
    InfluenceCapability cap;
    cap.Id = "bare_hand";
    cap.Channel = InfluenceChannel::Melee;
    cap.Targeting = InfluenceTargeting::Single;
    cap.Damage = DamageGroups::MeleeFleshy(fleshy_damage);
    cap.FullIntervalSec = 0.5f;
    cap.RangeBlocks = 2.5f;
    cap.SourceFatigueCost = 6.f;
    cap.Effects.Target.FlashStrength = 0.6f;
    cap.Effects.Target.DurationSec = 0.2f;
    cap.Effects.Source.FlashStrength = 0.15f;
    return cap;
  }
};

} // namespace cutum

#endif
