#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "World/Collision/WorldCollision.h"
#include "World/Core/BlockWorld.h"
#include "World/Math/CollisionVolume.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <unordered_map>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "collision_broadphase_subchunk_test: " << message << std::endl;
    std::exit(1);
  }
}

static void InstallStoneDefinition(
    const std::shared_ptr<cutum::UBlockDefinitionStorage> &definitions,
    cutum::BlockId stone_id)
{
  cutum::BlockDefinition stone;
  stone.Name = "stone";
  stone.Id = stone_id;
  stone.Physics = cutum::BlockPhysicsProfile::Solid();
  std::unordered_map<cutum::BlockId, cutum::BlockDefinition> by_id;
  by_id[stone_id] = stone;
  std::unordered_map<std::string, cutum::BlockId> name_to_id;
  name_to_id["stone"] = stone_id;
  definitions->ReplaceAll(std::move(by_id), std::move(name_to_id));
}

int main()
{
  constexpr cutum::BlockId kStone = 51;
  auto definitions = std::make_shared<cutum::UBlockDefinitionStorage>();
  InstallStoneDefinition(definitions, kStone);
  cutum::UBlockRegistry registry(nullptr, definitions);

  cutum::UBlockWorld block_world;
  block_world.SetBlock(glm::ivec3(0, 0, 0), kStone);

  cutum::UWorldCollision collision(block_world);
  collision.SetBlockRegistry(&registry);
  collision.SetBroadphaseEnabled(true);

  const cutum::CollisionVolume near_vol{glm::vec3(0.5f), glm::vec3(0.25f)};
  const cutum::CollisionVolume far_vol{glm::vec3(14.5f), glm::vec3(0.25f)};

  Expect(collision.CheckBlockCollisionVolume(near_vol),
         "near subchunk volume should collide with corner solid");
  Expect(!collision.CheckBlockCollisionVolume(far_vol),
         "far subchunk volume should miss via broadphase");

  std::cout << "collision_broadphase_subchunk_test: OK" << std::endl;
  return 0;
}
