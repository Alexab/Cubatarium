// Regression test for pointable raycast targeting (leaves / cross plants).
// Non-solid cutout and cross blocks must be dig/place targets in Pointable
// mode, while SolidOnly (creature LoS, probes) and fluids keep old semantics.
#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"
#include "World/Raycast/BlockRaycast.h"

#include <cstdlib>
#include <glm/glm.hpp>
#include <iostream>
#include <memory>
#include <unordered_map>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "block_raycast_target_test: " << message << std::endl;
    std::exit(1);
  }
}

namespace
{

constexpr cutum::BlockId kStone = 8;
constexpr cutum::BlockId kWater = 9;
constexpr cutum::BlockId kTallGrass = 10;
constexpr cutum::BlockId kLeaves = 11;

std::shared_ptr<cutum::UBlockDefinitionStorage> MakeDefinitions()
{
  auto definitions = std::make_shared<cutum::UBlockDefinitionStorage>();

  cutum::BlockDefinition stone;
  stone.Name = "stone";
  stone.Physics = cutum::BlockPhysicsProfile::Solid();

  cutum::BlockDefinition water;
  water.Name = "water";
  water.Physics = cutum::BlockPhysicsProfile::FromPreset("water");

  cutum::BlockDefinition tall_grass;
  tall_grass.Name = "tall_grass";
  tall_grass.Physics = cutum::BlockPhysicsProfile::Solid();
  tall_grass.Physics.Movement.Occupancy = 0.0f;
  tall_grass.Render.Style = cutum::BlockRenderStyle::Cross;

  cutum::BlockDefinition leaves;
  leaves.Name = "tree_leaves";
  leaves.Physics = cutum::BlockPhysicsProfile::Solid();
  leaves.Physics.Movement.Occupancy = 0.0f;
  leaves.Render.Style = cutum::BlockRenderStyle::Cutout;

  std::unordered_map<cutum::BlockId, cutum::BlockDefinition> by_id;
  by_id[kStone] = stone;
  by_id[kWater] = water;
  by_id[kTallGrass] = tall_grass;
  by_id[kLeaves] = leaves;
  std::unordered_map<std::string, cutum::BlockId> name_to_id;
  name_to_id["stone"] = kStone;
  name_to_id["water"] = kWater;
  name_to_id["tall_grass"] = kTallGrass;
  name_to_id["tree_leaves"] = kLeaves;
  definitions->ReplaceAll(std::move(by_id), std::move(name_to_id));
  return definitions;
}

// Blocks along +x starting from column min_x.
void PlaceRow(cutum::UBlockWorld &world, cutum::BlockId id, int min_x, int count)
{
  for (int i = 0; i < count; ++i)
  {
    world.SetBlock(glm::ivec3(min_x + i, 0, 0), id);
  }
}

} // namespace

int main()
{
  auto definitions = MakeDefinitions();
  cutum::UBlockRegistry registry(nullptr, definitions);
  // WorldPosToBlock floors (v + 0.5): 0.4 keeps the eye inside block layer 0.
  const glm::vec3 eye(0.5f, 0.4f, 0.4f);
  const glm::vec3 dir(1.0f, 0.0f, 0.0f);

  // Row: leaves(2) stone(4) tall_grass(6) water(8).
  {
    cutum::UBlockWorld world;
    world.SetFluidDefinitions(definitions.get());
    PlaceRow(world, kLeaves, 2, 1);
    PlaceRow(world, kStone, 4, 1);
    PlaceRow(world, kTallGrass, 6, 1);
    PlaceRow(world, kWater, 8, 1);

    const auto pointable = cutum::RaycastSolidBlocks(
        world, registry, eye, dir, 32.0f,
        cutum::BlockRaycastTargetMode::Pointable);
    Expect(pointable.has_value(), "pointable ray hits leaves");
    Expect(pointable->blockPos == glm::ivec3(2, 0, 0),
           "pointable ray stops at cutout leaves");
    Expect(pointable->faceNormal == glm::ivec3(-1, 0, 0),
           "leaf hit face normal faces the eye");
    Expect(pointable->distance > 0.9f && pointable->distance < 1.1f,
           "leaf hit distance at block boundary");

    const auto solid_only = cutum::RaycastSolidBlocks(world, registry, eye, dir,
                                                      32.0f);
    Expect(solid_only.has_value(), "solid-only ray still hits stone");
    Expect(solid_only->blockPos == glm::ivec3(4, 0, 0),
           "solid-only ray passes through leaves and stops at stone");
  }

  // Cross plants are pointable targets.
  {
    cutum::UBlockWorld world;
    world.SetFluidDefinitions(definitions.get());
    PlaceRow(world, kTallGrass, 2, 1);
    PlaceRow(world, kStone, 4, 1);

    const auto pointable = cutum::RaycastSolidBlocks(
        world, registry, eye, dir, 32.0f,
        cutum::BlockRaycastTargetMode::Pointable);
    Expect(pointable.has_value(), "pointable ray hits cross plant");
    Expect(pointable->blockPos == glm::ivec3(2, 0, 0),
           "pointable ray stops at cross plant");

    const auto solid_only = cutum::RaycastSolidBlocks(world, registry, eye, dir,
                                                      32.0f);
    Expect(solid_only.has_value() &&
               solid_only->blockPos == glm::ivec3(4, 0, 0),
           "solid-only ray passes through cross plant");
  }

  // Fluids are never dig targets; a water column stays pass-through.
  {
    cutum::UBlockWorld world;
    world.SetFluidDefinitions(definitions.get());
    PlaceRow(world, kWater, 1, 6);

    const auto pointable = cutum::RaycastSolidBlocks(
        world, registry, eye, dir, 32.0f,
        cutum::BlockRaycastTargetMode::Pointable);
    Expect(!pointable.has_value(), "pointable ray passes through water");
    const auto solid_only =
        cutum::RaycastSolidBlocks(world, registry, eye, dir, 32.0f);
    Expect(!solid_only.has_value(), "solid-only ray passes through water");
  }

  std::cout << "block_raycast_target_test: OK" << std::endl;
  return 0;
}
