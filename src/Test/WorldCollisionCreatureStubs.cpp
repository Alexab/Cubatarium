#include "Creatures/Core/Creature.h"
#include "World/Environment/WorldEnvironment.h"

namespace cutum
{

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

} // namespace cutum
