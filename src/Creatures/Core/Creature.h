#ifndef CREATURE_H
#define CREATURE_H

#include "Creatures/Core/CreatureBounds.h"
#include "Creatures/Core/CreatureIntent.h"
#include "Creatures/Core/CreatureInventory.h"
#include "Creatures/Locomotion/CreatureLocomotionController.h"
#include "Creatures/Locomotion/CreatureLocomotionFacts.h"
#include "Creatures/Locomotion/LocomotionTypes.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <string>

namespace cutum
{

class UWorld;
class ICreatureVisual;

class UCreature
{
public:
  UCreature(CreatureId Id, std::string typeId, glm::vec3 bodyOrigin,
            glm::vec3 eyeOffset);
  virtual ~UCreature();

  CreatureId GetId() const { return Id; }
  const std::string &GetTypeId() const { return TypeId; }
  const std::string &GetSkinId() const { return SkinId; }
  void SetSkinId(const std::string &Id) { SkinId = Id; }
  glm::vec3 GetBodyOrigin() const { return BodyOrigin; }
  void SetBodyOrigin(const glm::vec3 &v) { BodyOrigin = v; }
  glm::vec3 GetEyeOffset() const { return EyeOffset; }
  /// Feet on ground (collision / rig root). Same as body origin.
  glm::vec3 GetFeetPosition() const { return BodyOrigin; }
  glm::vec3 GetEyePosition() const;
  /// Eye used by locomotion (feet + view eye height when grounded).
  glm::vec3 GetLocomotionEye() const;
  float GetYaw() const { return Yaw; }
  float GetPitch() const { return Pitch; }
  float GetModelYawOffsetDeg() const { return ModelYawOffsetDeg; }
  void SetModelYawOffsetDeg(float degrees) { ModelYawOffsetDeg = degrees; }
  void SetOrientation(float yaw, float pitch);

  const CreatureBoundsState &GetBounds() const { return Bounds; }
  CreatureBoundsState &GetBoundsMutable() { return Bounds; }
  void SyncBoundsFromStance();
  void SyncFeetFromLocomotion(const UWorld &world,
                              glm::vec3 &eyeAfterLocomotion);

  CollisionVolume GetCollisionVolume() const;
  UCreatureLocomotionController &GetLocomotion() { return Locomotion; }
  const UCreatureLocomotionController &GetLocomotion() const
  {
    return Locomotion;
  }
  UCreatureInventory &GetInventory() { return Inventory; }
  const UCreatureInventory &GetInventory() const { return Inventory; }

  CreatureIntent GetIntent() const { return Intent; }
  void SetIntent(const CreatureIntent &intent) { Intent = intent; }
  void ClearIntent() { Intent = CreatureIntent{}; }

  LocomotionState GetLocomotionState() const { return LocomotionFacts.state; }
  const CreatureLocomotionFacts &GetLocomotionFacts() const
  {
    return LocomotionFacts;
  }
  CreatureMovementMode GetMovementMode() const { return Locomotion.GetMode(); }
  LocomotionArchetype GetLocomotionArchetype() const
  {
    return LocomotionArchetype;
  }
  void SetLocomotionArchetype(LocomotionArchetype archetype)
  {
    LocomotionArchetype = archetype;
  }
  void SetWalkCycleHz(float hz) { WalkCycleHz = hz; }
  void RebuildLocomotionFacts(const CreatureLocomotionRawInput &input,
                              const CreatureLocomotionCapabilities &caps,
                              const UWorld *world = nullptr);
  void RebuildLocomotionFactsFromController(
      const UCreatureLocomotionController &controller,
      const CreatureLocomotionCapabilities &caps, float dt,
      float horizontalSpeedOverride = -1.0f,
      const UWorld *world = nullptr);

  bool IsPlayerCharacter() const { return PlayerCharacter; }
  void SetPlayerCharacter(bool v) { PlayerCharacter = v; }
  bool IsPossessed() const { return Possessed; }
  void SetPossessed(bool v) { Possessed = v; }

  virtual bool IsPlayer() const { return false; }
  virtual void ExecuteIntent(UWorld &world, float dt);
  virtual void UpdateControlled(UWorld &world, const CreatureInput &input,
                                float dt);

  ICreatureVisual *GetVisual() { return Visual.get(); }
  void SetVisual(std::unique_ptr<ICreatureVisual> visual);

  void SetCapabilities(const CreatureLocomotionCapabilities &caps)
  {
    Locomotion.SetCapabilities(caps);
  }

protected:
  CreatureId Id;
  std::string TypeId;
  std::string SkinId;
  glm::vec3 BodyOrigin;
  glm::vec3 EyeOffset;
  float Yaw{0.0f};
  float Pitch{0.0f};
  float ModelYawOffsetDeg{0.0f};
  CreatureBoundsState Bounds;
  UCreatureLocomotionController Locomotion;
  UCreatureInventory Inventory;
  CreatureIntent Intent{};
  bool PlayerCharacter{false};
  bool Possessed{false};
  std::unique_ptr<ICreatureVisual> Visual;
  LocomotionArchetype LocomotionArchetype{
      LocomotionArchetype::TerrestrialBiped};
  CreatureLocomotionFacts LocomotionFacts{};
  glm::vec3 LastBodyOrigin{0.0f};
  float WalkCycleHz{2.0f};
};

} // namespace cutum

#endif
