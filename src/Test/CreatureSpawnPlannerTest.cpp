#include "Creatures/Definition/CreatureDefinition.h"
#include "Creatures/Spawning/CreatureSpawnPlanner.h"

#include <cmath>
#include <iostream>
#include <random>
#include <vector>

namespace
{

void Expect(bool condition, const char *message, int &failures)
{
  if (!condition)
  {
    std::cerr << "creature_spawn_planner_test: " << message << '\n';
    ++failures;
  }
}

cutum::CreatureDefinition MakeSpecies(const std::string &id, bool natural,
                                      float weight, bool hostile, int groupMin,
                                      int groupMax)
{
  cutum::CreatureDefinition def;
  def.Id = id;
  def.catalog.naturalSpawnable = natural;
  def.catalog.spawnWeight = weight;
  if (hostile)
  {
    def.catalog.tags.push_back("hostile");
  }
  def.catalog.spawnGroupMin = groupMin;
  def.catalog.spawnGroupMax = groupMax;
  return def;
}

} // namespace

int main()
{
  using namespace cutum;
  int failures = 0;

  {
    std::mt19937 rng{42};
    CreatureSpawnPlanner planner(rng);

    CreatureDefinition sheep =
        MakeSpecies("sheep", true, 1.f, false, 2, 4);
    CreatureDefinition wolf =
        MakeSpecies("wolf", true, 1.f, true, 1, 2);
    CreatureDefinition bot = MakeSpecies("bot", false, 1.f, false, 1, 1);
    CreatureDefinition zeroW = MakeSpecies("zero", true, 0.f, false, 1, 1);
    const std::vector<const CreatureDefinition *> spawnable = {
        &sheep, &wolf, &bot, &zeroW};

    const auto withRoom =
        planner.BuildEligibleOptions(spawnable, true, 2.f);
    Expect(withRoom.size() == 2,
           "non-natural and zero-weight species are never eligible", failures);
    Expect(withRoom[0].value == &sheep && withRoom[0].weight == 1.f,
           "passive species keep their base weight", failures);
    Expect(withRoom[1].value == &wolf && withRoom[2 - 1].weight == 2.f,
           "hostile weight scales by spawn pressure", failures);

    const auto withoutRoom =
        planner.BuildEligibleOptions(spawnable, false, 2.f);
    Expect(withoutRoom.size() == 1 && withoutRoom[0].value == &sheep,
           "hostile species are dropped without hostile room", failures);
  }

  {
    std::mt19937 rng{7};
    CreatureSpawnPlanner planner(rng);
    const glm::vec3 player(100.f, 64.f, -50.f);
    for (int i = 0; i < 500; ++i)
    {
      const glm::vec3 candidate = planner.NextCandidateOrigin(player);
      const glm::vec2 delta(candidate.x - player.x, candidate.z - player.z);
      const float dist = std::sqrt(delta.x * delta.x + delta.y * delta.y);
      Expect(dist >= 22.f - 1e-3f && dist <= 44.f + 1e-3f,
             "candidate ring distance stays within 22..44 blocks", failures);
      Expect(std::abs(candidate.y - player.y) <= 7.f,
             "candidate height offset stays within +-7 blocks", failures);
      if (failures > 8)
      {
        break;
      }
    }
    for (int i = 0; i < 200; ++i)
    {
      const float jitter = planner.NextGroupJitter();
      Expect(jitter >= -2.f && jitter <= 2.f,
             "group jitter stays within +-2 blocks", failures);
      const int groupSize = planner.NextGroupSize(2, 4);
      Expect(groupSize >= 2 && groupSize <= 4,
             "group size stays within the species range", failures);
      const float roll = planner.NextWeightRoll(13.5f);
      Expect(roll >= 0.f && roll <= 13.5f,
             "weighted roll stays within [0, total]", failures);
      if (failures > 12)
      {
        break;
      }
    }
  }

  {
    // Same seed -> same planning stream (world reload determinism).
    std::mt19937 rngA{0xC0BA};
    std::mt19937 rngB{0xC0BA};
    CreatureSpawnPlanner plannerA(rngA);
    CreatureSpawnPlanner plannerB(rngB);
    bool identical = true;
    for (int i = 0; i < 64; ++i)
    {
      identical = identical &&
                  plannerA.NextCandidateOrigin(glm::vec3(0.f)) ==
                      plannerB.NextCandidateOrigin(glm::vec3(0.f)) &&
                  plannerA.NextGroupJitter() == plannerB.NextGroupJitter() &&
                  plannerA.NextGroupSize(1, 4) == plannerB.NextGroupSize(1, 4);
    }
    Expect(identical, "same seed produces the same planning stream", failures);
  }

  if (failures != 0)
  {
    return 1;
  }
  std::cout << "creature_spawn_planner_test: OK\n";
  return 0;
}
