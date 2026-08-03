#include "Creatures/Influence/InfluenceHitMath.h"
#include "Creatures/Influence/InfluenceCapability.h"
#include "Creatures/Influence/InfluenceTypes.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "influence_hit_math_test: " << message << std::endl;
    std::exit(1);
  }
}

static bool Approx(float a, float b, float eps = 0.05f)
{
  return std::fabs(a - b) <= eps;
}

int main()
{
  using namespace cutum;

  {
    ArmorGroups armor = ArmorGroups::DefaultFleshy();
    InfluenceCapability cap = InfluenceCapability::DefaultBareHand(8);
    const auto full = InfluenceHitMath::Compute(armor, cap, 1.0f);
    Expect(full.DidHit, "full interval hits");
    Expect(Approx(full.Damage, 8.f), "full fleshy 100 → damage 8");
    Expect(Approx(full.IntervalMul, 1.f), "interval mul 1");
  }

  {
    ArmorGroups armor = ArmorGroups::DefaultFleshy();
    InfluenceCapability cap = InfluenceCapability::DefaultBareHand(10);
    cap.FullIntervalSec = 1.0f;
    const auto half = InfluenceHitMath::Compute(armor, cap, 0.5f);
    Expect(Approx(half.IntervalMul, 0.5f), "half interval");
    Expect(Approx(half.Damage, 5.f), "half damage 5");
  }

  {
    ArmorGroups armor = ArmorGroups::DefaultFleshy();
    armor.Ratings["immortal"] = 1;
    InfluenceCapability cap = InfluenceCapability::DefaultBareHand(8);
    const auto miss = InfluenceHitMath::Compute(armor, cap, 1.0f);
    Expect(!miss.DidHit && miss.Damage == 0.f, "immortal cancels");
  }

  {
    ArmorGroups armor;
    armor.Ratings["fleshy"] = 50;
    InfluenceCapability cap = InfluenceCapability::DefaultBareHand(8);
    const auto half_armor = InfluenceHitMath::Compute(armor, cap, 1.0f);
    Expect(Approx(half_armor.Damage, 4.f), "armor 50 → half damage");
  }

  {
    ArmorGroups armor = ArmorGroups::DefaultFleshy();
    InfluenceCapability cap = InfluenceCapability::DefaultBareHand(8);
    cap.Damage.Ratings["icy"] = 4;
    armor.Ratings["icy"] = 0;
    const auto only_fleshy = InfluenceHitMath::Compute(armor, cap, 1.0f);
    Expect(Approx(only_fleshy.Damage, 8.f), "missing icy armor adds 0");
  }

  std::cout << "influence_hit_math_test: ok" << std::endl;
  return 0;
}
