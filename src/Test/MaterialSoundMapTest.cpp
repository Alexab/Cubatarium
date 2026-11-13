#include "Audio/MaterialSoundMap.h"

#include <iostream>
#include <nlohmann/json.hpp>

namespace
{

constexpr const char *kTestName = "material_sound_map_test";

void Expect(bool cond, const char *msg)
{
  if (!cond)
  {
    std::cerr << kTestName << " FAIL: " << msg << "\n";
    std::exit(1);
  }
}

} // namespace

int main()
{
  const nlohmann::json root = nlohmann::json::parse(R"json({
    "surface_materials": {
      "grass": {"blocks": ["grass"], "dig_groups": ["crumbly"],
                "footstep_set": "footstep.grass", "dig_set": "dig.soft",
                "dug_set": "dug.soft"},
      "stone": {"blocks": ["stone"], "dig_groups": ["cracky"],
                "footstep_set": "footstep.stone", "dig_set": "dig.stone",
                "dug_set": "dug.stone"},
      "wood":  {"blocks": [], "dig_groups": ["choppy"],
                "footstep_set": "footstep.wood"}
    },
    "creature_defaults": {
      "terrestrial_biped": "footstep.barefoot",
      "aerial": ""
    },
    "biomes": {
      "forest": {"loop_set": "ambient.forest.loop",
                 "day_stingers": ["ambient.forest.bird"],
                 "night_stingers": ["ambient.forest.crickets"]}
    },
    "fallback_biome": {"loop_set": "ambient.wind.loop"},
    "weather": {"rain": "ambient.rain.loop"}
  })json");

  cutum::UMaterialSoundMap map;
  Expect(map.Load(&root), "manifest parses");

  // Exact block name wins.
  const std::uint32_t grassId = map.ResolveMaterial("grass", "");
  Expect(grassId != 0, "grass resolves by name");
  const cutum::SurfaceMaterialDef *grass = map.GetMaterial(grassId);
  Expect(grass && grass->FootstepSet == "footstep.grass", "grass footstep set");

  // Dig-group fallback: unknown block with cracky → stone.
  const std::uint32_t stoneId =
      map.ResolveMaterial("totally_unknown", "cracky:3,fleshy:1");
  Expect(stoneId != 0 && stoneId != grassId, "cracky falls back");
  const cutum::SurfaceMaterialDef *stone = map.GetMaterial(stoneId);
  Expect(stone && stone->Name == "stone", "fallback is stone");
  Expect(stone && stone->DugSet == "dug.stone", "stone dug set");

  // Wood via choppy when the block has no exact match.
  const std::uint32_t woodId = map.ResolveMaterial("door", "choppy:2");
  Expect(woodId != 0 && woodId != stoneId, "choppy resolves wood");

  // Priority: cracky is checked before crumbly/choppy.
  const std::uint32_t mixedId =
      map.ResolveMaterial("unknown", "choppy:1,cracky:1");
  Expect(mixedId == stoneId, "cracky outranks choppy");

  // No match anywhere → 0 and a safe nullptr.
  Expect(map.ResolveMaterial("unknown", "fleshy:2") == 0, "no material → 0");
  Expect(map.GetMaterial(0) == nullptr, "id 0 invalid");
  Expect(map.GetMaterial(999) == nullptr, "out of range invalid");

  // Creature defaults.
  Expect(map.FindCreatureDefault("terrestrial_biped") &&
             map.FindCreatureDefault("terrestrial_biped")->FootstepSet ==
                 "footstep.barefoot",
         "biped default");
  Expect(map.FindCreatureDefault("missing_archetype") == nullptr,
         "missing default");

  // Biome resolution: longest prefix, fallback otherwise.
  Expect(map.ResolveBiome("dense_forest")->LoopSet == "ambient.forest.loop",
         "prefix match");
  Expect(map.ResolveBiome("forest")->Biome == "forest", "exact match");
  Expect(map.ResolveBiome("desert")->LoopSet == "ambient.wind.loop",
         "fallback loop");
  const cutum::BiomeAmbienceDef &forest =
      *map.ResolveBiome("sparse_forest");
  Expect(forest.DayStingers.size() == 1, "day stinger loaded");
  Expect(forest.NightStingers.size() == 1, "night stinger loaded");

  Expect(map.FindWeather("rain") &&
             map.FindWeather("rain")->LoopSet == "ambient.rain.loop",
         "weather overlay");

  std::cout << kTestName << " OK\n";
  return 0;
}
