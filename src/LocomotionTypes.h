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

LocomotionArchetype ParseLocomotionArchetype(const std::string &s);
const char *ToString(LocomotionState state);
const char *ToString(LocomotionArchetype archetype);

struct CreatureInput
{
  bool moveForward{false};
  bool moveBack{false};
  bool moveLeft{false};
  bool moveRight{false};
  bool jumpHeld{false};
  bool jumpPressed{false};
  bool crouchHeld{false};
};

struct CreatureLocomotionCapabilities
{
  bool canFly{true};
  bool canCrouch{true};
  bool canJump{true};
  /// Feet rise in blocks at jump apex (used with shared gravity to derive jump
  /// speed).
  float jumpHeightBlocks{1.25f};
  float walkSpeed{3.0f};
  float flySpeed{3.0f};
};

} // namespace cutum

#endif
