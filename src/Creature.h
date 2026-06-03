#ifndef CREATURE_H
#define CREATURE_H

#include <cstdint>
#include <memory>
#include <string>
#include <glm/glm.hpp>
#include "CreatureBounds.h"
#include "CreatureIntent.h"
#include "CreatureInventory.h"
#include "CreatureLocomotionController.h"
#include "LocomotionTypes.h"

namespace cutum {

class World;
class ICreatureVisual;

class Creature {
public:
 Creature(CreatureId id, std::string typeId, glm::vec3 bodyOrigin, glm::vec3 eyeOffset);
 virtual ~Creature();

 CreatureId GetId() const { return id_; }
 const std::string& GetTypeId() const { return typeId_; }
 const std::string& GetSkinId() const { return skinId_; }
 void SetSkinId(const std::string& id) { skinId_ = id; }
 glm::vec3 GetBodyOrigin() const { return bodyOrigin_; }
 void SetBodyOrigin(const glm::vec3& v) { bodyOrigin_ = v; }
 glm::vec3 GetEyeOffset() const { return eyeOffset_; }
 glm::vec3 GetEyePosition() const;
 float GetYaw() const { return yaw_; }
 float GetPitch() const { return pitch_; }
 void SetOrientation(float yaw, float pitch);

 const CreatureBoundsState& GetBounds() const { return bounds_; }
 CreatureBoundsState& GetBoundsMutable() { return bounds_; }
 void SyncBoundsFromStance();

 CollisionVolume GetCollisionVolume() const;
 CreatureLocomotionController& GetLocomotion() { return locomotion_; }
 const CreatureLocomotionController& GetLocomotion() const { return locomotion_; }
 CreatureInventory& GetInventory() { return inventory_; }
 const CreatureInventory& GetInventory() const { return inventory_; }

 CreatureIntent GetIntent() const { return intent_; }
 void SetIntent(const CreatureIntent& intent) { intent_ = intent; }
 void ClearIntent() { intent_ = CreatureIntent{}; }

 LocomotionState GetLocomotionState() const { return locomotion_.GetLocomotionState(); }
 CreatureMovementMode GetMovementMode() const { return locomotion_.GetMode(); }

 bool IsPlayerCharacter() const { return playerCharacter_; }
 void SetPlayerCharacter(bool v) { playerCharacter_ = v; }
 bool IsPossessed() const { return possessed_; }
 void SetPossessed(bool v) { possessed_ = v; }

 virtual bool IsPlayer() const { return false; }
 virtual void ApplyIntent(World& world, float dt);
 virtual void UpdateControlled(World& world, const CreatureInput& input, float dt);

 ICreatureVisual* GetVisual() { return visual_.get(); }
 void SetVisual(std::unique_ptr<ICreatureVisual> visual);

 void SetCapabilities(const CreatureLocomotionCapabilities& caps) { locomotion_.SetCapabilities(caps); }

protected:
 CreatureId id_;
 std::string typeId_;
 std::string skinId_;
 glm::vec3 bodyOrigin_;
 glm::vec3 eyeOffset_;
 float yaw_{-90.0f};
 float pitch_{0.0f};
 CreatureBoundsState bounds_;
 CreatureLocomotionController locomotion_;
 CreatureInventory inventory_;
 CreatureIntent intent_{};
 bool playerCharacter_{false};
 bool possessed_{false};
 std::unique_ptr<ICreatureVisual> visual_;
};

} // namespace cutum

#endif
