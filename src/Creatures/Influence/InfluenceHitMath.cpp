#include "Creatures/Influence/InfluenceHitMath.h"
#include <algorithm>

namespace cutum
{

InfluenceHitParams InfluenceHitMath::Compute(const ArmorGroups &armor,
                                             const InfluenceCapability &cap,
                                             float time_from_last_punch_sec)
{
  InfluenceHitParams out;
  if (armor.IsImmortal())
  {
    return out;
  }
  const float interval =
      std::max(0.05f, cap.FullIntervalSec);
  out.IntervalMul = std::clamp(time_from_last_punch_sec / interval, 0.f, 1.f);
  float damage = 0.f;
  for (const auto &pair : cap.Damage.Ratings)
  {
    const int armor_rating = armor.Get(pair.first);
    damage += static_cast<float>(pair.second) * out.IntervalMul *
              (static_cast<float>(armor_rating) / 100.f);
  }
  out.Damage = damage;
  out.DidHit = damage > 0.f || !cap.Damage.Ratings.empty();
  if (cap.Damage.Ratings.empty())
  {
    out.DidHit = false;
  }
  return out;
}

} // namespace cutum
