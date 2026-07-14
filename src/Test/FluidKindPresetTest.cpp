#include "Blocks/BlockDefinitionStorage.h"
#include "World/Core/BlockWorld.h"
#include "World/Physics/FluidBlockResolver.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <unordered_map>

namespace
{

constexpr const char *kTestName = "fluid_kind_preset_test";
constexpr cutum::BlockId kStone = 8;
constexpr cutum::BlockId kWeirdWater = 9;
constexpr cutum::BlockId kWeirdLava = 11;

void Expect(bool condition, const char *message)
{
  if (!condition)
  {
    std::cerr << kTestName << ": " << message << std::endl;
    std::exit(1);
  }
}

std::shared_ptr<cutum::UBlockDefinitionStorage> MakeDefinitions()
{
  auto defs = std::make_shared<cutum::UBlockDefinitionStorage>();

  cutum::BlockDefinition stone;
  stone.Name = "stone";
  stone.Physics = cutum::BlockPhysicsProfile::Solid();

  cutum::BlockDefinition weirdWater;
  weirdWater.Name = "weird_water";
  weirdWater.Physics = cutum::BlockPhysicsProfile::FromPreset("water");
  weirdWater.Physics.FluidMaxLevel = 3; // Old heuristic would classify as lava.
  weirdWater.Physics.FluidKindPreset = cutum::FluidKind::Water;

  cutum::BlockDefinition weirdLava;
  weirdLava.Name = "weird_lava";
  weirdLava.Physics = cutum::BlockPhysicsProfile::FromPreset("lava");
  weirdLava.Physics.FluidMaxLevel = 7; // Old heuristic would classify as water.
  weirdLava.Physics.FluidKindPreset = cutum::FluidKind::Lava;

  std::unordered_map<cutum::BlockId, cutum::BlockDefinition> by_id;
  by_id[kStone] = stone;
  by_id[kWeirdWater] = weirdWater;
  by_id[kWeirdLava] = weirdLava;

  std::unordered_map<std::string, cutum::BlockId> by_name;
  by_name["stone"] = kStone;
  by_name["weird_water"] = kWeirdWater;
  by_name["weird_lava"] = kWeirdLava;

  defs->ReplaceAll(std::move(by_id), std::move(by_name));
  return defs;
}

} // namespace

int main()
{
  auto definitions = MakeDefinitions();

  Expect(cutum::UFluidBlockResolver::FluidKindFromBlockId(*definitions, kWeirdWater) ==
             cutum::FluidKind::Water,
         "FluidKind resolver respects explicit water preset");
  Expect(cutum::UFluidBlockResolver::FluidKindFromBlockId(*definitions, kWeirdLava) ==
             cutum::FluidKind::Lava,
         "FluidKind resolver respects explicit lava preset");

  cutum::UBlockWorld world;
  world.SetFluidDefinitions(definitions.get());

  world.SetBlock(glm::ivec3(0, 10, 0), kWeirdWater);
  Expect(world.GetFluidState(glm::ivec3(0, 10, 0)).GetKind() == cutum::FluidKind::Water,
         "SetBlock assigns water kind from preset");

  world.SetBlock(glm::ivec3(1, 10, 0), kWeirdLava);
  Expect(world.GetFluidState(glm::ivec3(1, 10, 0)).GetKind() == cutum::FluidKind::Lava,
         "SetBlock assigns lava kind from preset");

  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}
