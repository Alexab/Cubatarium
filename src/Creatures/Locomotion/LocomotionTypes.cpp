#include "Creatures/Locomotion/LocomotionTypes.h"
#include <iostream>

namespace cutum
{

LocomotionArchetype ParseLocomotionArchetype(const std::string &s)
{
  if (s == "terrestrial_biped" || s.empty())
  {
    return LocomotionArchetype::TerrestrialBiped;
  }
  if (s == "terrestrial_quadruped")
  {
    return LocomotionArchetype::TerrestrialQuadruped;
  }
  if (s == "aerial")
  {
    return LocomotionArchetype::Aerial;
  }
  if (s == "aquatic")
  {
    return LocomotionArchetype::Aquatic;
  }
  if (s == "serpentine")
  {
    return LocomotionArchetype::Serpentine;
  }
  std::cerr << "ParseLocomotionArchetype: unknown '" << s
            << "', using terrestrial_biped" << std::endl;
  return LocomotionArchetype::TerrestrialBiped;
}

const char *ToString(LocomotionState state)
{
  switch (state)
  {
  case LocomotionState::Idle:
    return "idle";
  case LocomotionState::Walk:
    return "walk";
  case LocomotionState::Run:
    return "run";
  case LocomotionState::Jump:
    return "Jump";
  case LocomotionState::Fall:
    return "fall";
  case LocomotionState::Crouch:
    return "crouch";
  case LocomotionState::Fly:
    return "fly";
  case LocomotionState::Glide:
    return "glide";
  case LocomotionState::Hover:
    return "hover";
  case LocomotionState::Swim:
    return "swim";
  case LocomotionState::Tread:
    return "tread";
  case LocomotionState::Slither:
    return "slither";
  case LocomotionState::Coil:
    return "coil";
  case LocomotionState::Action:
    return "Action";
  case LocomotionState::Count:
    return "count";
  }
  return "unknown";
}

const char *LocomotionStateCatalogKey(LocomotionState state)
{
  switch (state)
  {
  case LocomotionState::Idle:
    return "Idle";
  case LocomotionState::Walk:
    return "Walk";
  case LocomotionState::Run:
    return "Run";
  case LocomotionState::Jump:
    return "Jump";
  case LocomotionState::Fall:
    return "Fall";
  case LocomotionState::Crouch:
    return "Crouch";
  case LocomotionState::Fly:
    return "Fly";
  case LocomotionState::Glide:
    return "Glide";
  case LocomotionState::Hover:
    return "Hover";
  case LocomotionState::Swim:
    return "Swim";
  case LocomotionState::Tread:
    return "Tread";
  case LocomotionState::Slither:
    return "Slither";
  case LocomotionState::Coil:
    return "Coil";
  case LocomotionState::Action:
    return "Action";
  case LocomotionState::Count:
    return "Count";
  }
  return "Unknown";
}

CreatureHabitat ParseCreatureHabitat(const std::string &s)
{
  if (s == "aquatic")
  {
    return CreatureHabitat::Aquatic;
  }
  if (s == "aerial")
  {
    return CreatureHabitat::Aerial;
  }
  if (s == "amphibious")
  {
    return CreatureHabitat::Amphibious;
  }
  if (s == "lava")
  {
    return CreatureHabitat::Lava;
  }
  if (s == "terrestrial" || s.empty())
  {
    return CreatureHabitat::Terrestrial;
  }
  std::cerr << "ParseCreatureHabitat: unknown '" << s
            << "', using terrestrial" << std::endl;
  return CreatureHabitat::Terrestrial;
}

const char *ToString(CreatureHabitat habitat)
{
  switch (habitat)
  {
  case CreatureHabitat::Aquatic:
    return "aquatic";
  case CreatureHabitat::Aerial:
    return "aerial";
  case CreatureHabitat::Amphibious:
    return "amphibious";
  case CreatureHabitat::Lava:
    return "lava";
  case CreatureHabitat::Terrestrial:
    return "terrestrial";
  }
  return "terrestrial";
}

const char *ToString(LocomotionArchetype archetype)
{
  switch (archetype)
  {
  case LocomotionArchetype::TerrestrialBiped:
    return "terrestrial_biped";
  case LocomotionArchetype::TerrestrialQuadruped:
    return "terrestrial_quadruped";
  case LocomotionArchetype::Aerial:
    return "aerial";
  case LocomotionArchetype::Aquatic:
    return "aquatic";
  case LocomotionArchetype::Serpentine:
    return "serpentine";
  }
  return "unknown";
}

} // namespace cutum
