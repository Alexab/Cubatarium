#include "Creatures/Stats/CreatureStatsDefaults.h"
#include <algorithm>
#include <cctype>

namespace cutum
{
namespace
{

bool HasTag(const std::vector<std::string> &tags, const char *needle)
{
  for (const auto &t : tags)
  {
    if (t == needle)
    {
      return true;
    }
  }
  return false;
}

bool ContainsIgnoreCase(const std::string &hay, const char *needle)
{
  auto lower = [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  };
  std::string h;
  h.resize(hay.size());
  std::transform(hay.begin(), hay.end(), h.begin(), lower);
  std::string n = needle;
  std::transform(n.begin(), n.end(), n.begin(), lower);
  return h.find(n) != std::string::npos;
}

CreatureStatsTemplate MakePlayerLike()
{
  CreatureStatsTemplate t;
  t.vitals.maxHealth = 100.f;
  t.vitals.maxSatiety = 100.f;
  t.vitals.maxThirst = 100.f;
  t.vitals.maxFatigue = 100.f;
  t.vitals.maxBreath = 100.f;
  t.vitals.armor = 0.f;
  t.vitals.maxFatalWounds = 3;
  t.attributes = CreatureAttributes{};
  t.needsTick = true;
  t.vitals.FillFull();
  return t;
}

} // namespace

CreatureStatsTemplate
CreatureStatsDefaults::For(CreatureRole role,
                           const std::vector<std::string> &tags,
                           CreatureHabitat habitat, const std::string &speciesId)
{
  if (role == CreatureRole::ControlledDefault || role == CreatureRole::Bot)
  {
    return MakePlayerLike();
  }

  CreatureStatsTemplate t;
  t.needsTick = false;
  t.vitals.maxFatalWounds = 1;
  t.vitals.maxSatiety = 100.f;
  t.vitals.maxThirst = 100.f;
  t.vitals.maxFatigue = 100.f;
  t.vitals.maxBreath = 100.f;
  t.vitals.armor = 0.f;

  const bool hostile = HasTag(tags, "hostile") || HasTag(tags, "monster") ||
                       HasTag(tags, "mobs_monster");
  const bool predator = HasTag(tags, "predator") ||
                        ContainsIgnoreCase(speciesId, "wolf");
  const bool animal = HasTag(tags, "animal") || HasTag(tags, "mobs_animal") ||
                      HasTag(tags, "passive");
  const bool aquatic = habitat == CreatureHabitat::Aquatic ||
                       HasTag(tags, "marine") || HasTag(tags, "aquatic");
  const bool aerial = habitat == CreatureHabitat::Aerial ||
                      HasTag(tags, "aerial") || HasTag(tags, "bird");

  if (hostile)
  {
    t.vitals.maxHealth = 60.f;
    t.vitals.armor = 2.f;
    t.attributes.strength = 12;
    t.attributes.agility = 8;
    t.attributes.endurance = 11;
    t.attributes.accuracy = 8;
    t.attributes.intelligence = 4;
    t.attributes.luck = 5;
    t.attributes.perception = 10;
    if (ContainsIgnoreCase(speciesId, "skeleton"))
    {
      t.vitals.maxHealth = 40.f;
      t.attributes.accuracy = 12;
    }
    else if (ContainsIgnoreCase(speciesId, "zombie"))
    {
      t.vitals.maxHealth = 50.f;
    }
    else if (ContainsIgnoreCase(speciesId, "oerkki") ||
             ContainsIgnoreCase(speciesId, "dungeon"))
    {
      t.vitals.maxHealth = 80.f;
      t.attributes.strength = 14;
    }
  }
  else if (predator)
  {
    t.vitals.maxHealth = 30.f;
    t.attributes.strength = 10;
    t.attributes.agility = 14;
    t.attributes.endurance = 10;
    t.attributes.accuracy = 6;
    t.attributes.intelligence = 4;
    t.attributes.luck = 6;
    t.attributes.perception = 12;
  }
  else if (aquatic)
  {
    t.vitals.maxHealth = 30.f;
    t.attributes.strength = 6;
    t.attributes.agility = 10;
    t.attributes.endurance = 12;
    t.attributes.accuracy = 4;
    t.attributes.intelligence = 3;
    t.attributes.luck = 6;
    t.attributes.perception = 8;
  }
  else if (aerial)
  {
    t.vitals.maxHealth = 12.f;
    t.attributes.strength = 4;
    t.attributes.agility = 16;
    t.attributes.endurance = 8;
    t.attributes.accuracy = 6;
    t.attributes.intelligence = 4;
    t.attributes.luck = 8;
    t.attributes.perception = 12;
  }
  else if (animal)
  {
    t.vitals.maxHealth = 24.f;
    t.attributes.strength = 6;
    t.attributes.agility = 8;
    t.attributes.endurance = 10;
    t.attributes.accuracy = 4;
    t.attributes.intelligence = 3;
    t.attributes.luck = 6;
    t.attributes.perception = 8;
    if (ContainsIgnoreCase(speciesId, "cow") ||
        ContainsIgnoreCase(speciesId, "pig"))
    {
      t.vitals.maxHealth = 32.f;
    }
    else if (ContainsIgnoreCase(speciesId, "chicken") ||
             ContainsIgnoreCase(speciesId, "bunny") ||
             ContainsIgnoreCase(speciesId, "rat"))
    {
      t.vitals.maxHealth = 16.f;
      t.attributes.agility = 12;
    }
    else if (ContainsIgnoreCase(speciesId, "sheep"))
    {
      t.vitals.maxHealth = 20.f;
    }
  }
  else
  {
    t.vitals.maxHealth = 40.f;
    t.attributes.strength = 8;
    t.attributes.agility = 8;
    t.attributes.endurance = 8;
    t.attributes.accuracy = 6;
    t.attributes.intelligence = 4;
    t.attributes.luck = 6;
    t.attributes.perception = 8;
  }

  t.attributes.ClampAll();
  t.vitals.FillFull();
  return t;
}

} // namespace cutum
