#ifndef CREATURELOCOMOTIONFACTS_H
#define CREATURELOCOMOTIONFACTS_H

#include "Creatures/Locomotion/LocomotionTypes.h"
#include <glm/glm.hpp>

namespace cutum
{

class UCreatureLocomotionController;

struct CreatureLocomotionRawInput
{
  const UCreatureLocomotionController *locomotion{nullptr};
  glm::vec3 bodyOriginBefore{0.0f};
  glm::vec3 bodyOriginAfter{0.0f};
  float dt{0.0f};
  LocomotionState suggestedAnim{LocomotionState::Idle};
  bool hasSuggestedAnim{false};
  float intentMoveSpeed{0.0f};
  /// State derived on the previous frame (Run hysteresis).
  LocomotionState previousState{LocomotionState::Idle};
};

struct CreatureLocomotionFacts
{
  LocomotionArchetype archetype{LocomotionArchetype::TerrestrialBiped};
  LocomotionState state{LocomotionState::Idle};
  CreatureMovementMode mode{CreatureMovementMode::Walking};
  bool onGround{true};
  bool inFluid{false};
  bool onFluidBottom{false};
  float horizontalSpeed{0.0f};
  float verticalSpeed{0.0f};
  float stanceBlend{0.0f};
  float bodyYaw{0.0f};
  float bodyPitch{0.0f};
  glm::vec3 lookAtWorld{0.0f};
  float lookAtWeight{0.0f};
  float animPhase{0.0f};
};

float AdvanceAnimPhase(float phase, float horizontalSpeed, float walkCycleHz,
                       float walkSpeedRef, float dt);

void FillTerrestrialRawFacts(CreatureLocomotionFacts &out,
                             const CreatureLocomotionRawInput &input,
                             LocomotionArchetype archetype, float bodyYaw,
                             float bodyPitch);

/// Facts for a creature that is not simulated (its terrain chunk is not
/// loaded): it stands still, so it must not keep presenting the last walk /
/// fly state and speed (glTF clips kept walking in place). A flyer caught in
/// the air hovers; everything else idles. Phase and pose fields are kept.
void SettleFrozenLocomotionFacts(CreatureLocomotionFacts &facts);

void FinalizeLocomotionFacts(CreatureLocomotionFacts &facts,
                             const CreatureLocomotionCapabilities &caps,
                             const CreatureLocomotionRawInput &input,
                             float walkCycleHz, float dt);

} // namespace cutum

#endif
