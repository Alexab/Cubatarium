#include "LocomotionTypes.h"
#include <iostream>

namespace cutum {

LocomotionArchetype ParseLocomotionArchetype(const std::string& s)
{
 if (s == "terrestrial_biped" || s.empty()) {
  return LocomotionArchetype::TerrestrialBiped;
 }
 if (s == "terrestrial_quadruped") {
  return LocomotionArchetype::TerrestrialQuadruped;
 }
 if (s == "aerial") {
  return LocomotionArchetype::Aerial;
 }
 if (s == "aquatic") {
  return LocomotionArchetype::Aquatic;
 }
 if (s == "serpentine") {
  return LocomotionArchetype::Serpentine;
 }
 std::cerr << "ParseLocomotionArchetype: unknown '" << s << "', using terrestrial_biped" << std::endl;
 return LocomotionArchetype::TerrestrialBiped;
}

const char* ToString(LocomotionState state)
{
 switch (state) {
 case LocomotionState::Idle:
  return "idle";
 case LocomotionState::Walk:
  return "walk";
 case LocomotionState::Run:
  return "run";
 case LocomotionState::Jump:
  return "jump";
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
  return "action";
 case LocomotionState::Count:
  return "count";
 }
 return "unknown";
}

const char* ToString(LocomotionArchetype archetype)
{
 switch (archetype) {
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
