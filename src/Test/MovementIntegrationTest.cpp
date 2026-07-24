#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "Creatures/Player/PlayerCapsule.h"
#include "World/Collision/WorldCollision.h"
#include "World/Core/BlockWorld.h"

#include <cmath>
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

  const cutum::UWorldCollision::StepUpProbe flatProbe =
      collision.ProbeStepUp(eye, glm::vec3(1.0f, 0.0f, 0.0f), cap, 1.25f);
  Expect(!flatProbe.Valid, "flat floor should not expose a step-up ledge");

  // One-block riser in +X: player on (0,0,0) top, riser at (1,0,0), step (1,1,0).
  cutum::UBlockWorld step_world;
  step_world.SetBlock(glm::ivec3(0, 0, 0), kStone);
  step_world.SetBlock(glm::ivec3(1, 0, 0), kStone);
  step_world.SetBlock(glm::ivec3(1, 1, 0), kStone);
  cutum::UWorldCollision step_collision(step_world);
  step_collision.SetBlockRegistry(&registry);
  step_collision.SetEntityCollisionEnabled(false);

  const glm::vec3 stepEye(0.0f, 0.5f + cap.eyeHeight, 0.0f);
  const cutum::UWorldCollision::StepUpProbe frontProbe =
      step_collision.ProbeStepUp(stepEye, glm::vec3(1.0f, 0.0f, 0.0f), cap,
                                 1.25f);
  Expect(frontProbe.Valid, "frontal approach should find a 1-block step-up");

  glm::vec3 landing = stepEye;
  Expect(step_collision.GetStepUpLanding(stepEye, glm::vec3(1.0f, 0.0f, 0.0f),
                                         cap, 1.25f, landing),
         "frontal step-up landing should be valid");
  Expect(std::abs(landing.x - 1.0f) < 0.01f &&
             std::abs(landing.z - 0.0f) < 0.01f,
         "landing should be at step cell center without backward offset");

  // Corner diagonal intent must not diagonal-jump (axis-aligned only).
  step_world.SetBlock(glm::ivec3(0, 0, 1), kStone);
  step_world.SetBlock(glm::ivec3(1, 0, 1), kStone);
  step_world.SetBlock(glm::ivec3(1, 1, 1), kStone);
  step_collision.InvalidateChunkMovementSolid(glm::ivec3(0, 0, 0));
  const glm::vec3 cornerEye(0.0f, 0.5f + cap.eyeHeight, 0.0f);
  const glm::vec3 diag(0.707f, 0.0f, 0.707f);
  const cutum::UWorldCollision::StepUpProbe cornerProbe =
      step_collision.ProbeStepUp(cornerEye, diag, cap, 1.25f);
  if (cornerProbe.Valid)
  {
    Expect(std::abs(cornerProbe.MoveDir.z) < 0.01f ||
               std::abs(cornerProbe.MoveDir.x) < 0.01f,
           "corner step-up must stay axis-aligned, not diagonal");
    Expect(!(std::abs(cornerProbe.TargetPos.x - 1.0f) < 0.01f &&
             std::abs(cornerProbe.TargetPos.z - 1.0f) < 0.01f),
           "corner approach must not land on diagonal step cell");
  }

  std::cout << "movement_integration_test: OK" << std::endl;
  return 0;
}
