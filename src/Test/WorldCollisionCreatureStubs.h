#ifndef WORLDCOLLISIONCREATURESTUBS_H
#define WORLDCOLLISIONCREATURESTUBS_H

#include "World/Math/CollisionVolume.h"

#include <vector>

namespace cutum::test
{
/// Creature bodies seen by the stubbed UWorldEnvironment in collision unit tests.
void SetStubCreatureVolumes(std::vector<CollisionVolume> volumes);
} // namespace cutum::test

#endif
