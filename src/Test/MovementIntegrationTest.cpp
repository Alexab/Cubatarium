#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "Creatures/Player/PlayerCapsule.h"
#include "World/Collision/WorldCollision.h"
#include "World/Core/BlockWorld.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <unordered_map>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "movement_integration_test: " << message << std::endl;
    std::exit(1);
  }
}

static void InstallSolid(cutum::UBlockDefinitionStorage &storage, cutum::BlockId id,
                        const std::string &name)
{
  cutum::BlockDefinition def;
  def.Name = name;
  def.Id = id;
  def.Physics = cutum::BlockPhysicsProfile::Solid();
  std::unordered_map<cutum::BlockId, cutum::BlockDefinition> by_id;
  by_id[id] = def;
  std::unordered_map<std::string, cutum::BlockId> name_to_id;
  name_to_id[name] = id;
  storage.ReplaceAll(std::move(by_id), std::move(name_to_id));
}

int main()
{
  constexpr cutum::BlockId kStone = 12;
  auto definitions = std::make_shared<cutum::UBlockDefinitionStorage>();
  InstallSolid(*definitions, kStone, "stone");
  cutum::UBlockRegistry registry(nullptr, definitions);

  cutum::UBlockWorld block_world;
  for (int x = 0; x < 3; ++x)
  {
    block_world.SetBlock(glm::ivec3(x, 0, 0), kStone);
  }
  block_world.SetBlock(glm::ivec3(2, 1, 0), kStone);
  block_world.SetBlock(glm::ivec3(2, 2, 0), kStone);

  cutum::UWorldCollision collision(block_world);
  collision.SetBlockRegistry(&registry);
  collision.SetEntityCollisionEnabled(false);

  const cutum::PlayerCapsule cap;
  const glm::vec3 eye(0.5f, 0.5f + cap.eyeHeight, 0.5f);
  const glm::vec3 delta(2.0f, 0.0f, 0.0f);
  const glm::vec3 resolved = collision.ResolveMovement(eye, delta, cap, 0);
  Expect(resolved.x < eye.x + delta.x - 0.05f,
         "horizontal movement should stop before the wall");

  const cutum::UWorldCollision::StepUpProbe probe =
      collision.ProbeStepUp(eye, glm::vec3(1.0f, 0.0f, 0.0f), cap, 1.25f);
  Expect(!probe.Valid, "flat floor should not expose a step-up ledge");

  std::cout << "movement_integration_test: OK" << std::endl;
  return 0;
}
