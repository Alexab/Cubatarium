#ifndef CREATURE_H
#define CREATURE_H

#include "CreatureBounds.h"
#include "CreatureIntent.h"
#include "CreatureInventory.h"
#include "CreatureLocomotionController.h"
#include "CreatureLocomotionFacts.h"
#include "LocomotionTypes.h"
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
  UCreature(CreatureId id, std::string typeId, glm::vec3 bodyOrigin,
            glm::vec3 eyeOffset);
  virtual ~UCreature();

  CreatureId GetId() const { return Id; }
  const std::string &GetTypeId() const { return TypeId; }
  const std::string &GetSkinId() const { return SkinId; }
  void SetSkinId(const std::string &id) { SkinId = id; }
  glm::vec3 GetBodyOrigin() const { return BodyOrigin; }
  void SetBodyOrigin(const glm::vec3 &v) { BodyOrigin = v; }
  glm::vec3 GetEyeOffset() const { return EyeOffset; }
  /// Feet on ground (collision / rig root). Same as body origin.
  glm::vec3 GetFeetPosition() const { return BodyOrigin; }
  glm::vec3 GetEyePosition() const;
  /// Eye used by locomotion (feet + view eye height when grounded).
  glm::vec3 GetLocomotionEye() const;
  float GetYaw() const { return yaw_; }
  float GetPitch() const { return pitch_; }
  float GetModelYawOffsetDeg() const { return modelYawOffsetDeg_; }
  void SetModelYawOffsetDeg(float degrees) { modelYawOffsetDeg_ = degrees; }
  void SetOrientation(float yaw, float pitch);

  const CreatureBoundsState &GetBounds() const { return bounds_; }
  CreatureBoundsState &GetBoundsMutable() { return bounds_; }
  void SyncBoundsFromStance();
  void SyncFeetFromLocomotion(const UWorld &world,
                              glm::vec3 &eyeAfterLocomotion);

  CollisionVolume GetCollisionVolume() const;
  UCreatureLocomotionController &GetLocomotion() { return locomotion_; }
  const UCreatureLocomotionController &GetLocomotion() const
  {
    return locomotion_;
  }
  UCreatureInventory &GetInventory() { return inventory_; }
  const UCreatureInventory &GetInventory() const { return inventory_; }

  CreatureIntent GetIntent() const { return intent_; }
  void SetIntent(const CreatureIntent &intent) { intent_ = intent; }
  void ClearIntent() { intent_ = CreatureIntent{}; }

  LocomotionState GetLocomotionState() const { return locomotionFacts_.state; }
  const CreatureLocomotionFacts &GetLocomotionFacts() const
  {
    return locomotionFacts_;
  }
  CreatureMovementMode GetMovementMode() const { return locomotion_.GetMode(); }
  LocomotionArchetype GetLocomotionArchetype() const
  {
    return locomotionArchetype_;
  }
  void SetLocomotionArchetype(LocomotionArchetype archetype)
  {
    locomotionArchetype_ = archetype;
  }
  void SetWalkCycleHz(float hz) { walkCycleHz_ = hz; }
  void RebuildLocomotionFacts(const CreatureLocomotionRawInput &input,
                              const CreatureLocomotionCapabilities &caps);
  void RebuildLocomotionFactsFromController(
      const UCreatureLocomotionController &controller,
      const CreatureLocomotionCapabilities &caps, float dt,
      float horizontalSpeedOverride = -1.0f);

  bool IsPlayerCharacter() const { return playerCharacter_; }
  void SetPlayerCharacter(bool v) { playerCharacter_ = v; }
  bool IsPossessed() const { return possessed_; }
  void SetPossessed(bool v) { possessed_ = v; }

  virtual bool IsPlayer() const { return false; }
  virtual void ExecuteIntent(UWorld &world, float dt);
  virtual void UpdateControlled(UWorld &world, const CreatureInput &input,
                                float dt);

  ICreatureVisual *GetVisual() { return visual_.get(); }
  void SetVisual(std::unique_ptr<ICreatureVisual> visual);

  void SetCapabilities(const CreatureLocomotionCapabilities &caps)
  {
    locomotion_.SetCapabilities(caps);
  }

protected:
  CreatureId Id;
  std::string TypeId;
  std::string SkinId;
  glm::vec3 BodyOrigin;
  glm::vec3 EyeOffset;
  float yaw_{0.0f};
  float pitch_{0.0f};
  float modelYawOffsetDeg_{0.0f};
  CreatureBoundsState bounds_;
  UCreatureLocomotionController locomotion_;
  UCreatureInventory inventory_;
  CreatureIntent intent_{};
  bool playerCharacter_{false};
  bool possessed_{false};
  std::unique_ptr<ICreatureVisual> visual_;
  LocomotionArchetype locomotionArchetype_{
      LocomotionArchetype::TerrestrialBiped};
  CreatureLocomotionFacts locomotionFacts_{};
  glm::vec3 lastBodyOrigin_{0.0f};
  float walkCycleHz_{2.0f};
};

} // namespace cutum

#endif
