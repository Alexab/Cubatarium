#include "Creatures/Spawning/UCreatureSpawnPlanner.h"

#include "Creatures/Definition/CreatureDefinition.h"
#include <cmath>

namespace cutum
{

std::vector<WeightedSpawnOption<const CreatureDefinition *>>
UCreatureSpawnPlanner::BuildEligibleOptions(
    const std::vector<const CreatureDefinition *> &spawnable, bool hostileRoom,
    float hostilePressure) const
{
  std::vector<WeightedSpawnOption<const CreatureDefinition *>> eligible;
  for (const CreatureDefinition *definition : spawnable)
  {
    if (!definition || !definition->catalog.naturalSpawnable ||
        definition->catalog.spawnWeight <= 0.f)
    {
      continue;
    }
    const bool hostile = HasCreatureTag(definition->catalog.tags, "hostile");
    if (hostile && !hostileRoom)
    {
      continue;
    }
    const float weight = definition->catalog.spawnWeight *
                         (hostile ? hostilePressure : 1.f);
    if (weight > 0.f)
    {
      eligible.push_back({definition, weight});
    }
  }
  return eligible;
}

glm::vec3 UCreatureSpawnPlanner::NextCandidateOrigin(
    const glm::vec3 &playerOrigin)
{
  std::uniform_real_distribution<float> angle(0.f, 6.28318530718f);
  std::uniform_real_distribution<float> distance(22.f, 44.f);
  std::uniform_int_distribution<int> heightOffset(-7, 7);
  const float theta = angle(Rng);
  const float radius = distance(Rng);
  return playerOrigin + glm::vec3(std::cos(theta) * radius,
                                  static_cast<float>(heightOffset(Rng)),
                                  std::sin(theta) * radius);
}

float UCreatureSpawnPlanner::NextWeightRoll(float totalWeight)
{
  std::uniform_real_distribution<float> chooseWeight(0.f, totalWeight);
  return chooseWeight(Rng);
}

int UCreatureSpawnPlanner::NextGroupSize(int groupMin, int groupMax)
{
  std::uniform_int_distribution<int> groupSize(groupMin, groupMax);
  return groupSize(Rng);
}

float UCreatureSpawnPlanner::NextGroupJitter()
{
  std::uniform_real_distribution<float> groupOffset(-2.f, 2.f);
  return groupOffset(Rng);
}

} // namespace cutum
