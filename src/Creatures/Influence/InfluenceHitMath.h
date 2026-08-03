#ifndef INFLUENCE_HIT_MATH_H
#define INFLUENCE_HIT_MATH_H

#include "Creatures/Influence/InfluenceCapability.h"
#include "Creatures/Influence/InfluenceTypes.h"

namespace cutum
{

struct InfluenceHitParams
{
  float Damage{0.f};
  float IntervalMul{1.f};
  bool DidHit{false};
};

/// Luanti-style: damage = Σ damage[g] * clamp(dt/interval,0..1) * (armor[g]/100).
/// Immortal armor group cancels the hit.
struct InfluenceHitMath
{
  static InfluenceHitParams Compute(const ArmorGroups &armor,
                                    const InfluenceCapability &cap,
                                    float time_from_last_punch_sec);
};

} // namespace cutum

#endif
