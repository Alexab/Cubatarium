#include "Creatures/Core/CreatureIntent.h"
#include "Creatures/Influence/InfluenceCapability.h"
#include "Creatures/Influence/InfluenceHitMath.h"
#include "Creatures/Influence/InfluenceIntentUtil.h"
#include "Creatures/Influence/InfluenceTypes.h"
#include "Creatures/Stats/CreatureAttributes.h"
#include "Items/ToolCapabilities.h"

#include <cmath>
#include <iostream>
#include <cstdlib>

namespace
{
void Expect(bool condition, const char *message)
{
  if (!condition)
  {
    std::cerr << "creature_influence_channels_test: " << message << '\n';
    std::exit(1);
  }
}
} // namespace

int main()
{
  using namespace cutum;

  // --- Melee intent writing (the shared brain/player router path) ---
  CreatureIntent intent;
  SetMeleeInfluenceIntent(intent, 77);
  Expect(intent.Influence.Channel == InfluenceChannel::Melee,
         "melee intent writes the melee channel");
  Expect(intent.Influence.TargetId == 77, "melee intent carries the target");
  Expect(intent.clearOnApply, "fresh intent defaults to one-shot");

  // --- Hit math: melee channel against armor groups ---
  CreatureAttributes attrs;
  const int expectedBare = BareHandFleshyDamage(attrs);
  const InfluenceCapability cap =
      InfluenceCapability::DefaultBareHand(expectedBare);

  const InfluenceHitParams hit = InfluenceHitMath::Compute(
      ArmorGroups::DefaultFleshy(), cap, attrs, 10.f);
  Expect(hit.DidHit && !hit.Missed, "full-charge bare hand hits fleshy armor");
  Expect(hit.Damage > 0.f, "bare hand deals positive damage");

  // Cooldown scaling: hitting mid-interval scales damage and interval.
  const InfluenceHitParams half =
      InfluenceHitMath::Compute(ArmorGroups::DefaultFleshy(), cap, attrs,
                                cap.FullIntervalSec * 0.5f);
  Expect(half.Damage <= hit.Damage,
         "mid-interval punch does not out-damage a full punch");

  // --- Empty armor groups are immortal (Luanti semantics: a group with
  // rating 0 blocks that damage group entirely) ---
  ArmorGroups naked;
  const InfluenceHitParams noArmor =
      InfluenceHitMath::Compute(naked, cap, attrs, 10.f);
  Expect(!noArmor.DidHit && noArmor.Damage == 0.f,
         "empty armor groups nullify the hit (rating-0 = immune)");

  std::cout << "creature_influence_channels_test: OK\n";
  return 0;
}
