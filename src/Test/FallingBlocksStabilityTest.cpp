#include "Blocks/BlockDefinitionStorage.h"
#include "World/Core/BlockWorld.h"
#include "World/Physics/FallingBlockRules.h"
#include "World/Physics/Replay/WorldStateHasher.h"

#include <cstdlib>
#include <iostream>
#include <unordered_map>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "falling_blocks_stability_test: " << message << std::endl;
    std::exit(1);
  }
}

static cutum::UBlockDefinitionStorage MakeDefinitions(cutum::BlockId falling_id)
{
  cutum::BlockDefinition def;
  def.Name = "test_sand";
  def.Physics.Falling = true;
  std::unordered_map<cutum::BlockId, cutum::BlockDefinition> by_id;
  by_id[falling_id] = def;
  std::unordered_map<std::string, cutum::BlockId> name_to_id;
  name_to_id["test_sand"] = falling_id;

  cutum::UBlockDefinitionStorage storage;
  storage.ReplaceAll(std::move(by_id), std::move(name_to_id));
  return storage;
}

static uint64_t SimulateFallingColumn()
{
  constexpr cutum::BlockId kSand = 42;
  const cutum::UBlockDefinitionStorage definitions = MakeDefinitions(kSand);
  cutum::UBlockWorld world;

  for (int y = 1; y <= 4; ++y)
  {
    world.SetBlock(glm::ivec3(0, y, 0), kSand);
  }
  world.SetBlock(glm::ivec3(0, 0, 0), cutum::BLOCK_AIR);

  for (int step = 0; step < 8; ++step)
  {
    for (int y = 1; y <= 4; ++y)
    {
      cutum::UFallingBlockRules::TryApplyFall(definitions, world,
                                              glm::ivec3(0, y, 0));
    }
  }

  return cutum::UWorldStateHasher::HashBlockWorldRegion(world, glm::ivec3(0, 0, 0),
                                                        glm::ivec3(0, 4, 0));
}

int main()
{
  const uint64_t first = SimulateFallingColumn();
  const uint64_t second = SimulateFallingColumn();
  Expect(first == second, "falling column simulation must be deterministic");
  Expect(first != 0ULL, "falling column must change world state");

  std::cout << "falling_blocks_stability_test: OK" << std::endl;
  return 0;
}
