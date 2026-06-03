#include "TestMob.h"

namespace cutum {

TestMob::TestMob(CreatureId id, glm::vec3 bodyOrigin)
    : Creature(id, "test_mob", bodyOrigin, glm::vec3(0.0f, 1.45f, 0.0f))
{
 CreatureBoundsProfile profile;
 profile.restSizeBlocks = glm::vec3(0.8f, 1.6f, 0.8f);
 profile.minSizeBlocks = glm::vec3(0.8f, 0.8f, 0.8f);
 profile.maxSizeBlocks = glm::vec3(1.2f, 1.6f, 0.8f);
 GetBoundsMutable().profile = profile;
 GetBoundsMutable().currentSizeBlocks = profile.restSizeBlocks;
}

void TestMob::ApplyIntent(World& world, float dt)
{
 if (IsPossessed()) {
  return;
 }
 ai_.Tick(world, *this, dt);
 Creature::ApplyIntent(world, dt);
}

} // namespace cutum
