#ifndef CREATURE_ATTRIBUTES_H
#define CREATURE_ATTRIBUTES_H

#include <algorithm>

namespace cutum
{

struct CreatureAttributes
{
  int strength{10};
  int agility{10};
  int endurance{10};
  int accuracy{10};
  int intelligence{10};
  int luck{10};
  int perception{10};

  static int ClampAttr(int v) { return std::clamp(v, 1, 20); }

  void ClampAll()
  {
    strength = ClampAttr(strength);
    agility = ClampAttr(agility);
    endurance = ClampAttr(endurance);
    accuracy = ClampAttr(accuracy);
    intelligence = ClampAttr(intelligence);
    luck = ClampAttr(luck);
    perception = ClampAttr(perception);
  }
};

} // namespace cutum

#endif
