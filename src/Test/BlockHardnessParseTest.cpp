#include "Blocks/BlockDefinition.h"
#include "Blocks/BlockDigRules.h"

#include <cstdlib>
#include <iostream>
#include <nlohmann/json.hpp>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "block_hardness_parse_test: " << message << std::endl;
    std::exit(1);
  }
}

int main()
{
  {
    const nlohmann::json j = {{"name", "stone"},
                              {"textures", {"t", "t", "t", "t", "t", "t"}},
                              {"hardness", 1.5}};
    const cutum::ParsedBlockJson parsed = cutum::ParseBlockFromJson(j);
    Expect(parsed.Valid, "stone json valid");
    Expect(parsed.Definition.Hardness == 1.5f, "stone hardness 1.5");
  }

  {
    const nlohmann::json j = {{"name", "bedrock"},
                              {"textures", {"t", "t", "t", "t", "t", "t"}},
                              {"hardness", 0}};
    const cutum::ParsedBlockJson parsed = cutum::ParseBlockFromJson(j);
    Expect(parsed.Valid, "bedrock json valid");
    Expect(parsed.Definition.Hardness == 0.0f, "bedrock hardness 0");
  }

  {
    const nlohmann::json j = {{"name", "plain"},
                              {"textures", {"t", "t", "t", "t", "t", "t"}}};
    const cutum::ParsedBlockJson parsed = cutum::ParseBlockFromJson(j);
    Expect(parsed.Valid, "plain json valid");
    Expect(parsed.Definition.Hardness == 1.0f, "default hardness 1.0");
  }

  {
    const nlohmann::json j = {{"name", "neg"},
                              {"textures", {"t", "t", "t", "t", "t", "t"}},
                              {"hardness", -4}};
    const cutum::ParsedBlockJson parsed = cutum::ParseBlockFromJson(j);
    Expect(parsed.Valid, "negative hardness json valid");
    Expect(parsed.Definition.Hardness == 0.0f, "negative hardness clamped to 0");
  }

  Expect(cutum::BlockDigRules::CrackStageIndex(0.0f, 10) == 0, "stage at 0");
  Expect(cutum::BlockDigRules::CrackStageIndex(0.15f, 10) == 1, "stage at 0.15");
  Expect(cutum::BlockDigRules::CrackStageIndex(0.99f, 10) == 9, "stage at 0.99");
  Expect(cutum::BlockDigRules::CrackStageIndex(1.0f, 10) == 9, "stage at 1.0");

  std::cout << "block_hardness_parse_test: OK" << std::endl;
  return 0;
}
