#ifndef CREATURE_VITALS_H
#define CREATURE_VITALS_H

#include <algorithm>

namespace cutum
{

struct CreatureVitals
{
  float health{100.f};
  float maxHealth{100.f};
  float satiety{100.f};
  float maxSatiety{100.f};
  float thirst{100.f};
  float maxThirst{100.f};
  float fatigue{0.f};
  float maxFatigue{100.f};
  float breath{100.f};
  float maxBreath{100.f};
  float armor{0.f};
  int fatalWounds{0};
  int maxFatalWounds{1};

  void ClampCurrents()
  {
    maxHealth = std::max(1.f, maxHealth);
    maxSatiety = std::max(1.f, maxSatiety);
    maxThirst = std::max(1.f, maxThirst);
    maxFatigue = std::max(1.f, maxFatigue);
    maxBreath = std::max(1.f, maxBreath);
    armor = std::clamp(armor, 0.f, 20.f);
    maxFatalWounds = std::max(1, maxFatalWounds);
    health = std::clamp(health, 0.f, maxHealth);
    satiety = std::clamp(satiety, 0.f, maxSatiety);
    thirst = std::clamp(thirst, 0.f, maxThirst);
    fatigue = std::clamp(fatigue, 0.f, maxFatigue);
    breath = std::clamp(breath, 0.f, maxBreath);
    fatalWounds = std::clamp(fatalWounds, 0, maxFatalWounds);
  }

  void FillFull()
  {
    health = maxHealth;
    satiety = maxSatiety;
    thirst = maxThirst;
    fatigue = 0.f;
    breath = maxBreath;
    ClampCurrents();
  }
};

} // namespace cutum

#endif
