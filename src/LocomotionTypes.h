#ifndef LOCOMOTIONTYPES_H
#define LOCOMOTIONTYPES_H

namespace cutum {

enum class CreatureMovementMode { Walking, Flying };

enum class LocomotionState {
 Idle,
 Walk,
 Run,
 Jump,
 Fall,
 Crouch,
 Fly
};

struct CreatureInput {
 bool moveForward{false};
 bool moveBack{false};
 bool moveLeft{false};
 bool moveRight{false};
 bool jumpHeld{false};
 bool jumpPressed{false};
 bool crouchHeld{false};
};

struct CreatureLocomotionCapabilities {
 bool canFly{true};
 bool canCrouch{true};
 bool canJump{true};
};

} // namespace cutum

#endif
