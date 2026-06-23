#ifndef LOCOMOTIONTYPES_H
#define LOCOMOTIONTYPES_H

#include <cstdint>
#include <string>

namespace cutum
{

enum class CreatureMovementMode
{
  Walking,
  Flying
};

enum class LocomotionState : uint8_t
{
  Idle,
  Walk,
  Run,
  Jump,
  Fall,
  Crouch,
  Fly,
  Glide,
  Hover,
  Swim,
  Tread,
  Slither,
  Coil,
  Action,
  Count
};

enum class LocomotionArchetype : uint8_t
{
  TerrestrialBiped,
  TerrestrialQuadruped,
  Aerial,
  Aquatic,
  Serpentine
};

enum class CreatureHabitat : uint8_t
{
  Terrestrial,
  Aquatic,
  Aerial,
  Amphibious,
  Lava
};

LocomotionArchetype ParseLocomotionArchetype(const std::string &s);
CreatureHabitat ParseCreatureHabitat(const std::string &s);
const char *ToString(CreatureHabitat habitat);
const char *ToString(LocomotionState state);
const char *ToString(LocomotionArchetype archetype);

struct CreatureInput
{
  bool MoveForward{false};
  bool MoveBack{false};
  bool MoveLeft{false};
  bool MoveRight{false};
  bool jumpHeld{false};
  bool jumpPressed{false};
  bool crouchHeld{false};
  /// Desktop: hold Ctrl; Android: sprint toggle button state.
  bool sprintHeld{false};
};

struct CreatureLocomotionCapabilities
{
  bool canFly{true};
  bool canCrouch{true};
  bool canJump{true};
  bool canSprint{true};
  /// Feet rise in blocks at Jump apex (used with shared gravity to derive Jump
  /// speed).
  float jumpHeightBlocks{1.25f};
  float walkSpeed{3.0f};
  float flySpeed{3.0f};
  /// Multipliers relative to walk_speed (physics, not animation).
  float sprintSpeedMultiplier{1.3f};
  float crouchSpeedMultiplier{0.3f};
  float flySpeedMultiplier{2.0f};
};

} // namespace cutum

#endif
