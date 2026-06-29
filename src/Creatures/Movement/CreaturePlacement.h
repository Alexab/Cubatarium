#ifndef CREATUREPLACEMENT_H
#define CREATUREPLACEMENT_H

#include "Creatures/Locomotion/LocomotionTypes.h"
#include <glm/glm.hpp>
#include <vector>

namespace cutum
{

struct CreatureDefinition;
class UWorld;

enum class SpawnCollisionPolicy
{
  Creative,
  Full,
};

enum class SpawnFailureReason
{
  None,
  Habitat,
  Creature,
  Blocks,
};

struct PlacementResult
{
  glm::vec3 bodyOrigin{0.0f};
  SpawnFailureReason failure{SpawnFailureReason::Blocks};
};

inline std::vector<glm::ivec2> BuildSpawnSearchRing(int maxRadius)
{
  std::vector<glm::ivec2> cells;
  cells.reserve(static_cast<size_t>((maxRadius * 2 + 1) * (maxRadius * 2 + 1)));
  for (int r = 0; r <= maxRadius; ++r)
  {
    if (r == 0)
    {
      cells.emplace_back(0, 0);
      continue;
    }
    for (int dx = -r; dx <= r; ++dx)
    {
      cells.emplace_back(dx, -r);
      cells.emplace_back(dx, r);
    }
    for (int dz = -r + 1; dz < r; ++dz)
    {
      cells.emplace_back(-r, dz);
      cells.emplace_back(r, dz);
    }
  }
  return cells;
}

inline const char *SpawnFailureReasonLabel(SpawnFailureReason reason)
{
  switch (reason)
  {
  case SpawnFailureReason::None:
    return "none";
  case SpawnFailureReason::Habitat:
    return "habitat";
  case SpawnFailureReason::Creature:
    return "creature";
  case SpawnFailureReason::Blocks:
    return "blocks";
  }
  return "unknown";
}

SpawnFailureReason ClassifySpawnFailureAt(const UWorld &world,
                                          const CreatureDefinition &def,
                                          const glm::vec3 &bodyOrigin,
                                          SpawnCollisionPolicy policy);

PlacementResult FindSpawnOrigin(const UWorld &world,
                                const CreatureDefinition &def,
                                const glm::vec3 &viewProbe,
                                SpawnCollisionPolicy policy);

} // namespace cutum

#endif
