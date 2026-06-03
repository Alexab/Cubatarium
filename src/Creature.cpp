#include "Creature.h"
#include "CreatureDefinition.h"
#include "CreatureVisual.h"
#include "CreaturePartMeshData.h"
#include "CreatureWanderBehavior.h"
#include "World.h"
#include <cmath>
#include <cstdlib>

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
 return GetLocomotionEye();
}

glm::vec3 Creature::GetLocomotionEye() const
{
 const float eyeY = locomotion_.IsFeetAnchored()
                        ? locomotion_.GetFeetY() + locomotion_.GetViewEyeHeight()
                        : bodyOrigin_.y + locomotion_.GetViewEyeHeight();
 return glm::vec3(bodyOrigin_.x, eyeY, bodyOrigin_.z);
}

void Creature::SetOrientation(float yaw, float pitch)
{
 yaw_ = yaw;
 pitch_ = pitch;
}

void Creature::SyncBoundsFromStance()
{
 bounds_ = LerpBoundsStance(bounds_, locomotion_.GetStanceBlend());
 locomotion_.SetCollisionProfile(bounds_.profile.restSizeBlocks, eyeOffset_.y);
}

CollisionVolume Creature::GetCollisionVolume() const
{
 return CollisionVolumeFromBody(bodyOrigin_, bounds_.profile.restSizeBlocks);
}

void Creature::SyncFeetFromLocomotion(const glm::vec3& eyeAfterLocomotion)
{
 bodyOrigin_.x = eyeAfterLocomotion.x;
 bodyOrigin_.z = eyeAfterLocomotion.z;
 if (locomotion_.IsFeetAnchored()) {
  bodyOrigin_.y = locomotion_.GetFeetY();
 } else {
  bodyOrigin_.y = eyeAfterLocomotion.y - eyeOffset_.y;
 }
}

void Creature::TickWanderTimer(float dt, float /*intervalMin*/, float /*intervalMax*/)
{
 wanderTimer_ -= dt;
}

void Creature::ResetWanderTimer(float intervalMin, float intervalMax)
{
 const float span = intervalMax - intervalMin;
 wanderTimer_ = intervalMin + static_cast<float>(std::rand() % 1001) / 1000.0f * span;
}

void Creature::ApplyIntent(World& world, float dt)
{
 if (!possessed_) {
  if (const CreatureDefinition* def = world.GetCreatureDefinition(typeId_)) {
   if (def->behavior.id == "wander") {
    ApplyWanderIntent(*this, def->behavior, dt);
   }
  }
 }

 const float moveLen = glm::length(glm::vec2(intent_.moveDirWorld.x, intent_.moveDirWorld.z));
 if (!possessed_ && moveLen > 1e-4f) {
  yaw_ = ModelYawFromDirection(intent_.moveDirWorld.x, intent_.moveDirWorld.z);
  pitch_ = 0.0f;
 }

 if (!possessed_ && locomotion_.GetMode() == CreatureMovementMode::Walking) {
  glm::vec3 eye = GetLocomotionEye();
  CreatureInput emptyInput;
  locomotion_.UpdateLocomotion(&world, eye, emptyInput, dt, id_);
  SyncFeetFromLocomotion(eye);
  SyncBoundsFromStance();
 }

 if (possessed_) {
  if (intent_.clearOnApply) {
   ClearIntent();
  }
  return;
 }

 if (intent_.moveDirWorld != glm::vec3(0.0f)) {
  glm::vec3 delta = intent_.moveDirWorld * intent_.moveSpeed * dt;
  delta.y = 0.0f;
  bodyOrigin_ = world.ResolveMovementBody(bodyOrigin_, delta, bounds_.profile.restSizeBlocks, id_);
 }

 if (intent_.clearOnApply) {
  ClearIntent();
 }
}

void Creature::UpdateControlled(World& world, const CreatureInput& input, float dt)
{
 ClearIntent();
 glm::vec3 eye = GetLocomotionEye();
 locomotion_.UpdateLocomotion(&world, eye, input, dt, id_);
 SyncFeetFromLocomotion(eye);
 SyncBoundsFromStance();
}

} // namespace cutum
