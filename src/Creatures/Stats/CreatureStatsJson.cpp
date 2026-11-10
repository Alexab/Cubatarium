#include "Creatures/Stats/CreatureStatsJson.h"

namespace cutum
{

void CreatureStatsJson::WriteVitalsTemplate(nlohmann::json &out,
                                            const CreatureVitals &vitals)
{
  out["max_health"] = vitals.maxHealth;
  out["max_satiety"] = vitals.maxSatiety;
  out["max_thirst"] = vitals.maxThirst;
  out["max_fatigue"] = vitals.maxFatigue;
  out["max_breath"] = vitals.maxBreath;
  out["max_fatal_wounds"] = vitals.maxFatalWounds;
  out["armor"] = vitals.armor;
}

void CreatureStatsJson::WriteAttributes(nlohmann::json &out,
                                        const CreatureAttributes &attrs)
{
  out["strength"] = attrs.strength;
  out["agility"] = attrs.agility;
  out["endurance"] = attrs.endurance;
  out["accuracy"] = attrs.accuracy;
  out["intelligence"] = attrs.intelligence;
  out["luck"] = attrs.luck;
  out["perception"] = attrs.perception;
}

void CreatureStatsJson::ReadVitalsTemplate(const nlohmann::json &in,
                                           CreatureVitals &vitals)
{
  vitals.maxHealth = in.value("max_health", vitals.maxHealth);
  vitals.maxSatiety = in.value("max_satiety", vitals.maxSatiety);
  vitals.maxThirst = in.value("max_thirst", vitals.maxThirst);
  vitals.maxFatigue = in.value("max_fatigue", vitals.maxFatigue);
  vitals.maxBreath = in.value("max_breath", vitals.maxBreath);
  vitals.maxFatalWounds = in.value("max_fatal_wounds", vitals.maxFatalWounds);
  vitals.armor = in.value("armor", vitals.armor);
}

void CreatureStatsJson::ReadAttributes(const nlohmann::json &in,
                                       CreatureAttributes &attrs)
{
  attrs.strength = in.value("strength", attrs.strength);
  attrs.agility = in.value("agility", attrs.agility);
  attrs.endurance = in.value("endurance", attrs.endurance);
  attrs.accuracy = in.value("accuracy", attrs.accuracy);
  attrs.intelligence = in.value("intelligence", attrs.intelligence);
  attrs.luck = in.value("luck", attrs.luck);
  attrs.perception = in.value("perception", attrs.perception);
  attrs.ClampAll();
}

void CreatureStatsJson::Write(nlohmann::json &out, const CreatureVitals &vitals,
                              const CreatureAttributes &attrs)
{
  nlohmann::json v;
  v["health"] = vitals.health;
  v["max_health"] = vitals.maxHealth;
  v["satiety"] = vitals.satiety;
  v["max_satiety"] = vitals.maxSatiety;
  v["thirst"] = vitals.thirst;
  v["max_thirst"] = vitals.maxThirst;
  v["fatigue"] = vitals.fatigue;
  v["max_fatigue"] = vitals.maxFatigue;
  v["breath"] = vitals.breath;
  v["max_breath"] = vitals.maxBreath;
  v["armor"] = vitals.armor;
  v["fatal_wounds"] = vitals.fatalWounds;
  v["max_fatal_wounds"] = vitals.maxFatalWounds;
  out["vitals"] = v;

  nlohmann::json a;
  WriteAttributes(a, attrs);
  out["attributes"] = a;
}

bool CreatureStatsJson::Read(const nlohmann::json &in, CreatureVitals &vitals,
                             CreatureAttributes &attrs)
{
  bool any = false;
  if (in.contains("vitals") && in["vitals"].is_object())
  {
    any = true;
    const auto &v = in["vitals"];
    ReadVitalsTemplate(v, vitals);
    vitals.health = v.value("health", vitals.maxHealth);
    vitals.satiety = v.value("satiety", vitals.maxSatiety);
    vitals.thirst = v.value("thirst", vitals.maxThirst);
    vitals.fatigue = v.value("fatigue", 0.f);
    vitals.breath = v.value("breath", vitals.maxBreath);
    vitals.fatalWounds = v.value("fatal_wounds", vitals.fatalWounds);
    vitals.ClampCurrents();
  }
  if (in.contains("attributes") && in["attributes"].is_object())
  {
    any = true;
    ReadAttributes(in["attributes"], attrs);
  }
  return any;
}

} // namespace cutum
