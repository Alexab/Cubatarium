#include "Creatures/Influence/InfluenceHitMath.h"
#include "Items/ToolCapabilities.h"

namespace cutum
{

InfluenceHitParams InfluenceHitMath::Compute(const ArmorGroups &armor,
                                             const InfluenceCapability &cap,
                                             const CreatureAttributes &attrs,
                                             float time_from_last_punch_sec)
{
  ToolCapabilitiesDef tool;
  tool.FullPunchInterval = cap.FullIntervalSec;
  tool.PunchAttackUses = cap.PunchAttackUses;
  tool.Damage.Groups = cap.Damage.Ratings;
  const HitParams hit =
      ResolveHitParams(armor, tool, attrs, time_from_last_punch_sec);
  InfluenceHitParams out;
  out.Damage = hit.Damage;
  out.IntervalMul = hit.IntervalMul;
  out.WearDelta = hit.WearDelta;
  out.DidHit = hit.DidHit;
  out.Missed = hit.Missed;
  return out;
}

} // namespace cutum
