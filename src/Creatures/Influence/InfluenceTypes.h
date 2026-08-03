#ifndef INFLUENCE_TYPES_H
#define INFLUENCE_TYPES_H

#include <string>
#include <unordered_map>

namespace cutum
{

enum class InfluenceChannel
{
  None = 0,
  Melee,
  Ranged,
  Aura,
  Use
};

enum class InfluenceTargeting
{
  Single = 0,
  Cone,
  Radius
};

/// Named integer ratings (Luanti-style groups). Missing key → rating 0.
using InfluenceGroupMap = std::unordered_map<std::string, int>;

struct ArmorGroups
{
  InfluenceGroupMap Ratings;

  static ArmorGroups DefaultFleshy()
  {
    ArmorGroups g;
    g.Ratings["fleshy"] = 100;
    return g;
  }

  int Get(const std::string &name) const
  {
    const auto it = Ratings.find(name);
    return it == Ratings.end() ? 0 : it->second;
  }

  bool IsImmortal() const { return Get("immortal") > 0; }
};

struct DamageGroups
{
  InfluenceGroupMap Ratings;

  static DamageGroups MeleeFleshy(int amount)
  {
    DamageGroups g;
    g.Ratings["fleshy"] = amount;
    return g;
  }

  int Get(const std::string &name) const
  {
    const auto it = Ratings.find(name);
    return it == Ratings.end() ? 0 : it->second;
  }
};

} // namespace cutum

#endif
