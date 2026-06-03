#include "Creature.h"
#include "CreatureVisual.h"
#include "World.h"

namespace cutum {

Creature::Creature(CreatureId id, std::string typeId, glm::vec3 bodyOrigin, glm::vec3 eyeOffset)
    : id_(id)
    , typeId_(std::move(typeId))
    , bodyOrigin_(bodyOrigin)
    , eyeOffset_(eyeOffset)
{
 bounds_.profile.restSizeBlocks = glm::vec3(0.6f, 1.8f, 0.6f);
 bounds_.profile.minSizeBlocks = glm::vec3(0.6f, 1.5f, 0.6f);
 bounds_.profile.maxSizeBlocks = glm::vec3(0.6f, 1.8f, 0.6f);
 bounds_.currentSizeBlocks = bounds_.profile.restSizeBlocks;
 locomotion_.Reset();
}

Creature::~Creature() = default;

void Creature::SetVisual(std::unique_ptr<ICreatureVisual> visual)
{
 visual_ = std::move(visual);
}

glm::vec3 Creature::GetEyePosition() const
{
 return BoundsEyePosition(bodyOrigin_, eyeOffset_);
}

void Creature::SetOrientation(float yaw, float pitch)
{
 yaw_ = yaw;
 pitch_ = pitch;
}

void Creature::SyncBoundsFromStance()
{
 bounds_ = LerpBoundsStance(bounds_, locomotion_.GetStanceBlend());
}

CollisionVolume Creature::GetCollisionVolume() const
{
 return CollisionVolumeFromBody(bodyOrigin_, bounds_.currentSizeBlocks);
}

void Creature::ApplyIntent(World& world, float dt)
{
 if (possessed_ || intent_.moveDirWorld == glm::vec3(0.0f)) {
  return;
 }
 glm::vec3 delta = intent_.moveDirWorld * intent_.moveSpeed * dt;
 delta.y = 0.0f;
 bodyOrigin_ = world.ResolveMovementBody(bodyOrigin_, delta, bounds_.currentSizeBlocks);

 if (locomotion_.GetMode() == CreatureMovementMode::Walking) {
  glm::vec3 eye = GetEyePosition();
  CreatureInput emptyInput;
  locomotion_.UpdateLocomotion(&world, eye, emptyInput, dt);
  bodyOrigin_ = BodyOriginFromEye(eye, eyeOffset_);
  SyncBoundsFromStance();
 }

 if (intent_.clearOnApply) {
  ClearIntent();
 }
}

void Creature::UpdateControlled(World& world, const CreatureInput& input, float dt)
{
 ClearIntent();
 glm::vec3 eye = GetEyePosition();
 locomotion_.UpdateLocomotion(&world, eye, input, dt);
 bodyOrigin_ = BodyOriginFromEye(eye, eyeOffset_);
 SyncBoundsFromStance();
}

} // namespace cutum
