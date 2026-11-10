#ifndef INFLUENCE_HIT_MATH_H
#define INFLUENCE_HIT_MATH_H

#include "Creatures/Influence/InfluenceCapability.h"
#include "Creatures/Influence/InfluenceTypes.h"
#include "Creatures/Stats/CreatureAttributes.h"

namespace cutum
{

struct InfluenceHitParams
{
  float Damage{0.f};
  float IntervalMul{1.f};
  float WearDelta{0.f};
  bool DidHit{false};
  bool Missed{false};
};

/// Thin adapter over ToolCapabilities::ResolveHitParams (SoT hit math).
struct InfluenceHitMath
{
  static InfluenceHitParams Compute(const ArmorGroups &armor,
                                    const InfluenceCapability &cap,
                                    const CreatureAttributes &attrs,
                                    float time_from_last_punch_sec);
};

} // namespace cutum

#endif
