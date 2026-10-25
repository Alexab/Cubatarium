#ifndef CREATURESPAWNPLANNER_H
#define CREATURESPAWNPLANNER_H

#include "Creatures/Spawning/CreatureSpawnSelection.h"
#include <glm/glm.hpp>
#include <random>
#include <vector>

namespace cutum
{

struct CreatureDefinition;

/// Pure random-planning half of the natural spawner: species eligibility
/// with difficulty pressure, ring candidates around the player, weighted
/// species choice, group size and group jitter. Extracted from
/// UWorldEnvironment::TickNaturalPopulation so planning is unit-testable
/// without a world (audit phase 3, S5).
class CreatureSpawnPlanner
{
public:
  explicit CreatureSpawnPlanner(std::mt19937 &rng) : Rng(rng) {}

  /// Natural-spawnable species with positive weight; hostile species are
  /// dropped without hostile room and their weight scaled by pressure.
  std::vector<WeightedSpawnOption<const CreatureDefinition *>>
  BuildEligibleOptions(
      const std::vector<const CreatureDefinition *> &spawnable,
      bool hostileRoom, float hostilePressure) const;

  /// One ring candidate: uniform angle, 22..44 blocks out, ±7 height.
  glm::vec3 NextCandidateOrigin(const glm::vec3 &playerOrigin);

  /// Uniform roll in [0, totalWeight] for the weighted choice.
  float NextWeightRoll(float totalWeight);

  /// Group size drawn from the species' spawn_group_min..max.
  int NextGroupSize(int groupMin, int groupMax);

  /// Group member jitter: uniform in [-2, +2] blocks on one axis.
  float NextGroupJitter();

private:
  std::mt19937 &Rng;
};

} // namespace cutum

#endif
