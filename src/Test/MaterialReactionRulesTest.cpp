#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"
#include "World/Physics/MaterialReactionRules.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <unordered_map>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "material_reaction_rules_test: " << message << std::endl;
    std::exit(1);
  }
}

static void InstallReactionBlocks(
    const std::shared_ptr<cutum::UBlockDefinitionStorage> &definitions)
{
  cutum::BlockDefinition water;
  water.Name = "water";
  water.Id = 10;
  water.Physics.IsLiquid = true;
  water.Physics.LiquidRenewable = true;

  cutum::BlockDefinition lava;
  lava.Name = "lava";
  lava.Id = 11;
  lava.Physics.IsLiquid = true;
  lava.Physics.LiquidRenewable = false;

  cutum::BlockDefinition stone;
  stone.Name = "stone";
  stone.Id = 12;
  stone.Physics = cutum::BlockPhysicsProfile::Solid();

  cutum::BlockDefinition grass;
  grass.Name = "grass";
  grass.Id = 13;
  grass.Physics = cutum::BlockPhysicsProfile::Solid();
  grass.Physics.Flammable = true;

  cutum::BlockDefinition fire;
  fire.Name = "fire";
  fire.Id = 14;
  fire.Physics = cutum::BlockPhysicsProfile::Solid();

  std::unordered_map<cutum::BlockId, cutum::BlockDefinition> by_id;
  by_id[10] = water;
  by_id[11] = lava;
  by_id[12] = stone;
  by_id[13] = grass;
  by_id[14] = fire;
  std::unordered_map<std::string, cutum::BlockId> name_to_id;
  name_to_id["water"] = 10;
  name_to_id["lava"] = 11;
  name_to_id["stone"] = 12;
  name_to_id["grass"] = 13;
  name_to_id["fire"] = 14;
  definitions->ReplaceAll(std::move(by_id), std::move(name_to_id));
}

int main()
{
  auto definitions = std::make_shared<cutum::UBlockDefinitionStorage>();
  InstallReactionBlocks(definitions);
  cutum::UBlockRegistry registry(nullptr, definitions);
  cutum::UBlockWorld block_world;

  block_world.SetBlock(glm::ivec3(-1, 0, 0), 10);
  block_world.SetBlock(glm::ivec3(1, 0, 0), 11);
  block_world.SetBlock(glm::ivec3(0, 0, 0), cutum::BLOCK_AIR);

  cutum::UMaterialReactionRules rules;
  rules.ShadowMode = false;
  const std::vector<cutum::MaterialReactionResult> results =
      rules.EvaluateNeighbors(block_world, registry, glm::ivec3(0, 0, 0));

  Expect(!results.empty(), "water+lava neighbors should produce a reaction");
  Expect(results.front().Applied, "reaction should be applied");
  Expect(block_world.GetBlock(glm::ivec3(0, 0, 0)) == 12,
         "contact cell should become stone");

  block_world.SetBlock(glm::ivec3(0, 1, 0), 14);
  block_world.SetBlock(glm::ivec3(1, 1, 0), 13);
  const std::vector<cutum::MaterialReactionResult> fire_results =
      rules.EvaluateNeighbors(block_world, registry, glm::ivec3(1, 1, 0));
  Expect(!fire_results.empty(), "fire should spread to flammable neighbor");
  Expect(block_world.GetBlock(glm::ivec3(1, 1, 0)) == 14,
         "flammable neighbor should become fire");

  std::cout << "material_reaction_rules_test: OK" << std::endl;
  return 0;
}
