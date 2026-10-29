#include "Creatures/Core/Creature.h"
#include "World/Environment/WorldEnvironment.h"

#include "Test/WorldCollisionCreatureStubs.h"

#include <vector>

namespace cutum
{

namespace
{
std::vector<CollisionVolume> &StubCreatureVolumes()
{
  static std::vector<CollisionVolume> volumes;
  return volumes;
}
} // namespace

namespace test
{
void SetStubCreatureVolumes(std::vector<CollisionVolume> volumes)
{
  StubCreatureVolumes() = std::move(volumes);
}
} // namespace test

bool UWorldEnvironment::CheckCreatureCollisionVolume(const CollisionVolume &vol,
                                                     CreatureId) const
{
  for (const CollisionVolume &other : StubCreatureVolumes())
  {
    const glm::vec3 d = glm::abs(vol.center - other.center);
    const glm::vec3 reach = vol.halfExtents + other.halfExtents;
    if (d.x < reach.x && d.y < reach.y && d.z < reach.z)
    {
      return true;
    }
  }
  return false;
}

// These unit targets exercise voxel/world collision and ray traversal only.
// Keep the unrelated creature depenetration dependencies linkable without
// bringing the full creature and environment runtime into the test binaries.
CollisionVolume UCreature::GetCollisionVolume() const { return {}; }

UCreature *UWorldEnvironment::GetCreature(CreatureId) { return nullptr; }

std::vector<CreatureNeighborView>
UWorldEnvironment::QueryCreatureNeighborsInRadius(const glm::vec3 &, float,
                                                  CreatureId) const
{
  return {};
}

std::vector<CreatureNeighborView>
UWorldEnvironment::QueryCreatureNeighborsOverlappingVolume(
    const CollisionVolume &, CreatureId) const
{
  return {};
}

} // namespace cutum
