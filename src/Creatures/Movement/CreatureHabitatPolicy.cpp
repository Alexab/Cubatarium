#include "Creatures/Movement/CreatureHabitatPolicy.h"

namespace cutum
{

bool TerrestrialCanWalkOn(const EnvironmentSample &env)
{
  return !env.inWater && !env.inLava &&
         (env.onSolidGround || env.bodyBlocked);
}

bool HabitatAllows(CreatureHabitat habitat, HabitatContext ctx,
                   const EnvironmentSample &env)
{
  switch (ctx)
  {
  case HabitatContext::WanderCurrent:
    switch (habitat)
    {
    case CreatureHabitat::Terrestrial:
      return !env.inWater && !env.inLava;
    case CreatureHabitat::Amphibious:
      return !env.inLava;
    case CreatureHabitat::Aquatic:
      return env.inWater;
    case CreatureHabitat::Aerial:
      return env.inOpenAir && !env.inFluid;
    case CreatureHabitat::Lava:
      return env.inLava;
    }
    return false;

  case HabitatContext::WanderTarget:
    if (habitat == CreatureHabitat::Terrestrial)
    {
      return TerrestrialCanWalkOn(env);
    }
    if (habitat == CreatureHabitat::Amphibious)
    {
      if (env.inLava)
      {
        return false;
      }
      if (env.inWater)
      {
        return HabitatMatches(habitat, env);
      }
      return TerrestrialCanWalkOn(env);
    }
    return HabitatMatches(habitat, env);

  case HabitatContext::MoveApply:
    if (habitat == CreatureHabitat::Terrestrial)
    {
      return TerrestrialCanWalkOn(env);
    }
    if (habitat == CreatureHabitat::Amphibious)
    {
      if (env.inLava)
      {
        return false;
      }
      if (env.inWater)
      {
        return true;
      }
      return TerrestrialCanWalkOn(env);
    }
    return HabitatMatches(habitat, env);

  case HabitatContext::Spawn:
    return HabitatMatches(habitat, env);
  }
  return false;
}

} // namespace cutum
