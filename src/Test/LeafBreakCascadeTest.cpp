// Regression tests for BreakUnsupportedVegetationCluster: after breaking a
// block, the 26-connected non-solid cluster around it is cleared only when
// nothing movement-blocking anchors it (chopped-through crowns, floaters),
// while stumps, terrain and mid-trunk breaks keep their vegetation.
#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"
#include "World/Objects/ObjectUtil.h"

#include <cstdlib>
#include <glm/glm.hpp>
#include <iostream>
#include <memory>
#include <unordered_map>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "leaf_break_cascade_test: " << message << std::endl;
    std::exit(1);
  }
}

namespace
{

constexpr cutum::BlockId kStone = 8;
constexpr cutum::BlockId kLog = 12;
constexpr cutum::BlockId kLeaves = 11;

std::shared_ptr<cutum::UBlockDefinitionStorage> MakeDefinitions()
{
  auto definitions = std::make_shared<cutum::UBlockDefinitionStorage>();
  cutum::BlockDefinition stone;
  stone.Name = "stone";
  stone.Physics = cutum::BlockPhysicsProfile::Solid();
  cutum::BlockDefinition log;
  log.Name = "tree_log";
  log.Physics = cutum::BlockPhysicsProfile::Solid();
  cutum::BlockDefinition leaves;
  leaves.Name = "tree_leaves";
  leaves.Physics = cutum::BlockPhysicsProfile::Solid();
  leaves.Physics.Movement.Occupancy = 0.0f;
  leaves.Render.Style = cutum::BlockRenderStyle::Cutout;

  std::unordered_map<cutum::BlockId, cutum::BlockDefinition> by_id;
  by_id[kStone] = stone;
  by_id[kLog] = log;
  by_id[kLeaves] = leaves;
  std::unordered_map<std::string, cutum::BlockId> name_to_id;
  name_to_id["stone"] = kStone;
  name_to_id["tree_log"] = kLog;
  name_to_id["tree_leaves"] = kLeaves;
  definitions->ReplaceAll(std::move(by_id), std::move(name_to_id));
  return definitions;
}

/// Mimics the DelBlockAt sequence under test: clear the broken cell, run the
/// vertical support cascade, then the vegetation cluster cleanup.
std::vector<glm::ivec3> BreakAt(cutum::UBlockWorld &world,
                                cutum::UBlockRegistry &registry,
                                glm::ivec3 pos)
{
  world.SetBlock(pos, cutum::BLOCK_AIR);
  cutum::BreakUnsupportedBlocksAbove(world, registry, pos);
  return cutum::BreakUnsupportedVegetationCluster(world, registry, pos);
}

/// 3x3x3 leaf cube centered at c, skipping cells already occupied (trunk).
void PlaceCrown(cutum::UBlockWorld &world, glm::ivec3 c)
{
  for (int dx = -1; dx <= 1; ++dx)
    for (int dy = -1; dy <= 1; ++dy)
      for (int dz = -1; dz <= 1; ++dz)
      {
        const glm::ivec3 p = c + glm::ivec3(dx, dy, dz);
        if (world.GetBlock(p) == cutum::BLOCK_AIR)
        {
          world.SetBlock(p, kLeaves);
        }
      }
}

int CountInRegion(cutum::UBlockWorld &world, glm::ivec3 a, glm::ivec3 b,
                  cutum::BlockId id)
{
  int count = 0;
  for (int x = a.x; x <= b.x; ++x)
    for (int y = a.y; y <= b.y; ++y)
      for (int z = a.z; z <= b.z; ++z)
      {
        if (world.GetBlock(glm::ivec3(x, y, z)) == id)
        {
          ++count;
        }
      }
  return count;
}

} // namespace

int main()
{
  auto definitions = MakeDefinitions();
  cutum::UBlockRegistry registry(nullptr, definitions);

  // T1: crown attached through the top log stays on the stump; once the log
  // below it is chopped too, the whole crown detaches and is cleared.
  {
    cutum::UBlockWorld world;
    world.SetFluidDefinitions(definitions.get());
    for (int y = 0; y <= 3; ++y)
    {
      world.SetBlock(glm::ivec3(0, y, 0), kLog);
    }
    PlaceCrown(world, glm::ivec3(0, 4, 0));

    BreakAt(world, registry, glm::ivec3(0, 3, 0));
    Expect(CountInRegion(world, {-2, 2, -2}, {2, 6, 2}, kLeaves) > 0,
           "crown stays anchored to the remaining trunk");
    Expect(world.GetBlock(glm::ivec3(0, 2, 0)) == kLog,
           "lower trunk intact after top-log break");

    BreakAt(world, registry, glm::ivec3(0, 2, 0));
    Expect(CountInRegion(world, {-2, 2, -2}, {2, 6, 2}, kLeaves) == 0,
           "crown detached once its last log support is chopped");
    Expect(world.GetBlock(glm::ivec3(0, 1, 0)) == kLog &&
               world.GetBlock(glm::ivec3(0, 0, 0)) == kLog,
           "remaining trunk stays solid");
  }

  // T2: breaking a mid-trunk log removes no vegetation at all.
  {
    cutum::UBlockWorld world;
    world.SetFluidDefinitions(definitions.get());
    for (int y = 0; y <= 4; ++y)
    {
      world.SetBlock(glm::ivec3(0, y, 0), kLog);
    }
    PlaceCrown(world, glm::ivec3(0, 5, 0));
    const int leaves_before =
        CountInRegion(world, {-2, 3, -2}, {2, 7, 2}, kLeaves);

    BreakAt(world, registry, glm::ivec3(0, 2, 0));
    Expect(CountInRegion(world, {-2, 3, -2}, {2, 7, 2}, kLeaves) ==
               leaves_before,
           "mid-trunk break keeps the whole crown");
    Expect(world.GetBlock(glm::ivec3(0, 3, 0)) == kLog &&
               world.GetBlock(glm::ivec3(0, 4, 0)) == kLog,
           "trunk above the break stays");
  }

  // T3: a generation-style floater in the air vanishes when poked.
  {
    cutum::UBlockWorld world;
    world.SetFluidDefinitions(definitions.get());
    world.SetBlock(glm::ivec3(0, 10, 0), kLeaves);
    world.SetBlock(glm::ivec3(1, 10, 0), kLeaves);
    world.SetBlock(glm::ivec3(1, 11, 0), kLeaves);
    world.SetBlock(glm::ivec3(2, 11, 0), kLeaves);
    world.SetBlock(glm::ivec3(2, 11, 1), kLeaves);

    const auto removed = BreakAt(world, registry, glm::ivec3(0, 10, 0));
    Expect(removed.size() == 4, "whole floater cluster removed");
    Expect(CountInRegion(world, {-2, 8, -2}, {4, 13, 3}, kLeaves) == 0,
           "no floater leaves remain");
  }

  // T4: a bush resting on the ground keeps its leaves when poked.
  {
    cutum::UBlockWorld world;
    world.SetFluidDefinitions(definitions.get());
    world.SetBlock(glm::ivec3(0, 0, 0), kStone);
    world.SetBlock(glm::ivec3(0, 1, 0), kLeaves);
    world.SetBlock(glm::ivec3(1, 1, 0), kLeaves);
    world.SetBlock(glm::ivec3(0, 2, 0), kLeaves);

    BreakAt(world, registry, glm::ivec3(0, 2, 0));
    Expect(world.GetBlock(glm::ivec3(0, 1, 0)) == kLeaves &&
               world.GetBlock(glm::ivec3(1, 1, 0)) == kLeaves,
           "terrain-anchored bush survives a top-leaf break");
  }

  // T5: oversized clusters are left untouched (cascade bound).
  {
    cutum::UBlockWorld world;
    world.SetFluidDefinitions(definitions.get());
    world.SetBlock(glm::ivec3(0, 10, 0), kLeaves);
    world.SetBlock(glm::ivec3(1, 10, 0), kLeaves);
    world.SetBlock(glm::ivec3(2, 10, 0), kLeaves);
    world.SetBlock(glm::ivec3(3, 10, 0), kLeaves);

    world.SetBlock(glm::ivec3(0, 10, 0), cutum::BLOCK_AIR);
    const auto removed = cutum::BreakUnsupportedVegetationCluster(
        world, registry, glm::ivec3(0, 10, 0), /*maxClusterBlocks=*/2);
    Expect(removed.empty(), "oversized cluster aborts removal");
    Expect(CountInRegion(world, {0, 10, 0}, {3, 10, 0}, kLeaves) == 3,
           "oversized cluster leaves world untouched");
  }

  std::cout << "leaf_break_cascade_test: OK" << std::endl;
  return 0;
}
