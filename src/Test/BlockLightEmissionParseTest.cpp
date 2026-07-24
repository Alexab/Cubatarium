#include "Blocks/BlockDefinition.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"

#include <cstdlib>
#include <iostream>
#include <nlohmann/json.hpp>
#include <unordered_map>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "block_light_emission_parse_test: " << message << std::endl;
    std::exit(1);
  }
}

int main()
{
  {
    const nlohmann::json j = {
        {"name", "torch"},
        {"textures", {"t", "t", "t", "t", "t", "t"}},
        {"lighting", {{"emission", 14}}}};
    const cutum::ParsedBlockJson parsed = cutum::ParseBlockFromJson(j);
    Expect(parsed.Valid, "torch json valid");
    Expect(parsed.Definition.Lighting.Emission == 14, "torch emission 14");
  }

  {
    const nlohmann::json j = {
        {"name", "bright"},
        {"textures", {"t", "t", "t", "t", "t", "t"}},
        {"lighting", {{"emission", 20}}}};
    const cutum::ParsedBlockJson parsed = cutum::ParseBlockFromJson(j);
    Expect(parsed.Valid, "clamp json valid");
    Expect(parsed.Definition.Lighting.Emission == 15, "emission clamped to 15");
  }

  {
    const nlohmann::json j = {{"name", "dark"},
                              {"textures", {"t", "t", "t", "t", "t", "t"}},
                              {"lighting", {{"emission", -3}}}};
    const cutum::ParsedBlockJson parsed = cutum::ParseBlockFromJson(j);
    Expect(parsed.Valid, "negative json valid");
    Expect(parsed.Definition.Lighting.Emission == 0, "emission clamped to 0");
  }

  {
    const nlohmann::json j = {{"name", "plain"},
                              {"textures", {"t", "t", "t", "t", "t", "t"}}};
    const cutum::ParsedBlockJson parsed = cutum::ParseBlockFromJson(j);
    Expect(parsed.Valid, "plain json valid");
    Expect(parsed.Definition.Lighting.Emission == 0, "default emission 0");
  }

  auto definitions = std::make_shared<cutum::UBlockDefinitionStorage>();
  cutum::BlockDefinition torch;
  torch.Name = "torch";
  torch.Id = 50;
  torch.Lighting.Emission = 14;
  std::unordered_map<cutum::BlockId, cutum::BlockDefinition> by_id;
  by_id[50] = torch;
  std::unordered_map<std::string, cutum::BlockId> name_to_id;
  name_to_id["torch"] = 50;
  definitions->ReplaceAll(std::move(by_id), std::move(name_to_id));
  cutum::UBlockRegistry registry(nullptr, definitions);
  Expect(registry.GetLightEmission(50) == 14, "registry emission");

  std::cout << "block_light_emission_parse_test: OK" << std::endl;
  return 0;
}
