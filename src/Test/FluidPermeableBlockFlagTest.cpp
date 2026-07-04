#include "Blocks/BlockDefinition.h"
#include "World/Physics/FluidPermeabilityUtil.h"

#include <cstdlib>
#include <iostream>
#include <nlohmann/json.hpp>

namespace
{

constexpr const char *kTestName = "fluid_permeable_block_flag_test";

nlohmann::json MakeBlockJson(const nlohmann::json &physics,
                             const std::string &style)
{
  return nlohmann::json{
      {"name", "test_block"},
      {"physics", physics},
      {"render", {{"style", style}, {"transparent", true}}},
      {"textures", {"a", "a", "a", "a", "a", "a"}}};
}

void Expect(bool condition, const char *message)
{
  if (!condition)
  {
    std::cerr << kTestName << ": " << message << std::endl;
    std::exit(1);
  }
}

} // namespace

int main()
{
  using namespace cutum;

  ParsedBlockJson fallback = ParseBlockFromJson(
      MakeBlockJson({{"movement", {{"occupancy", 0.0f}}}}, "cross"));
  fallback.Definition.Id = 100;
  Expect(
      IsFluidPermeableFromDefinition(fallback.Definition.Id, &fallback.Definition,
                                     fallback.Definition.Physics.IsLiquid),
      "fallback to render style + occupancy is enabled");

  ParsedBlockJson disabled = ParseBlockFromJson(
      MakeBlockJson({{"movement", {{"occupancy", 0.0f}}}, {"fluid_permeable", false}},
                    "cross"));
  disabled.Definition.Id = 101;
  Expect(
      !IsFluidPermeableFromDefinition(disabled.Definition.Id, &disabled.Definition,
                                      disabled.Definition.Physics.IsLiquid),
      "explicit fluid_permeable=false overrides fallback");

  ParsedBlockJson forced = ParseBlockFromJson(
      MakeBlockJson({{"movement", {{"occupancy", 1.0f}}}, {"fluid_permeable", true}},
                    "cube"));
  forced.Definition.Id = 102;
  Expect(
      IsFluidPermeableFromDefinition(forced.Definition.Id, &forced.Definition,
                                     forced.Definition.Physics.IsLiquid),
      "explicit fluid_permeable=true forces permeability");

  ParsedBlockJson liquid_forced = ParseBlockFromJson(
      MakeBlockJson(
          {{"preset", "water"}, {"liquid", true}, {"fluid_permeable", true}},
          "fluid"));
  liquid_forced.Definition.Id = 103;
  Expect(
      !IsFluidPermeableFromDefinition(liquid_forced.Definition.Id,
                                      &liquid_forced.Definition,
                                      liquid_forced.Definition.Physics.IsLiquid),
      "liquid blocks are never treated as permeable decor");

  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}
