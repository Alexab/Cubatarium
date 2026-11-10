#include "Creatures/Core/CreatureCatalogTypes.h"
#include "WorldGen/Sampling/BiomeRegistry.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace
{

namespace fs = std::filesystem;

int gFailures = 0;

void Require(bool condition, const std::string &message)
{
  if (!condition)
  {
    std::cerr << "FAIL " << message << '\n';
    ++gFailures;
  }
}

const std::vector<std::string> kHostile{"mobs", "hostile"};
const std::vector<std::string> kPassive{"mobs", "passive"};

void TestActiveTimeRules()
{
  using cutum::AllowsNaturalSpawnTime;
  using cutum::CreatureActiveTime;

  // Hostiles without explicit data default to the night window.
  Require(AllowsNaturalSpawnTime(kHostile, CreatureActiveTime::Any,
                                 /*isNight=*/true, /*underground=*/false),
          "hostile default spawns at night");
  Require(!AllowsNaturalSpawnTime(kHostile, CreatureActiveTime::Any,
                                  /*isNight=*/false, /*underground=*/false),
          "hostile default does not spawn at day");
  // Underground is always dark: cave spawns bypass the window.
  Require(AllowsNaturalSpawnTime(kHostile, CreatureActiveTime::Any, false,
                                 /*underground=*/true),
          "hostile default spawns underground at day");
  // Explicit data wins over the hostile default.
  Require(AllowsNaturalSpawnTime(kHostile, CreatureActiveTime::Day, false,
                                 false),
          "explicit day hostile spawns at day");
  Require(!AllowsNaturalSpawnTime(kHostile, CreatureActiveTime::Day, true,
                                  false),
          "explicit day hostile does not spawn at night");
  // Passives default to any time.
  Require(AllowsNaturalSpawnTime(kPassive, CreatureActiveTime::Any, true,
                                 false) &&
              AllowsNaturalSpawnTime(kPassive, CreatureActiveTime::Any, false,
                                     false),
          "passive default spawns at any time");
  Require(!AllowsNaturalSpawnTime(kPassive, CreatureActiveTime::Night, false,
                                  false),
          "explicit night passive (owl) does not spawn at day");
}

void TestBiomeGate()
{
  cutum::CreatureCatalogInfo forestOnly;
  forestOnly.spawnBiomes = {"forest", "hills"};
  Require(cutum::SpeciesAllowedInBiome(forestOnly, "forest"),
          "listed biome allowed");
  Require(!cutum::SpeciesAllowedInBiome(forestOnly, "desert"),
          "unlisted biome rejected");
  cutum::CreatureCatalogInfo anywhere;
  Require(cutum::SpeciesAllowedInBiome(anywhere, "desert") &&
              cutum::SpeciesAllowedInBiome(anywhere, "ocean"),
          "empty list means any biome");
}

void TestCatalogData()
{
  const fs::path creaturesRoot =
      fs::path(CUBATARIUM_SOURCE_DIR) / "models" / "creatures";
  int naturalSpecies = 0;
  for (const auto &entry : fs::directory_iterator(creaturesRoot))
  {
    const fs::path catalogPath = entry.path() / "creature.json";
    if (!fs::is_regular_file(catalogPath))
    {
      continue;
    }
    std::ifstream file(catalogPath);
    nlohmann::json data;
    file >> data;
    if (!data.value("catalog", nlohmann::json::object())
             .value("natural_spawnable", false))
    {
      continue;
    }
    ++naturalSpecies;
    const auto &catalog = data["catalog"];
    const std::string species = entry.path().filename().string();

    Require(catalog.value("spawn_weight", 0.f) > 0.f,
            species + ": natural species needs a positive spawn_weight");
    if (catalog.contains("spawn_biomes"))
    {
      Require(catalog["spawn_biomes"].is_array(), species +
            ": spawn_biomes must be an array");
      for (const auto &biome : catalog["spawn_biomes"])
      {
        Require(biome.is_string() &&
                    cutum::BiomeIdFromString(biome.get<std::string>()) !=
                        cutum::BiomeId::Invalid,
                species + ": unknown biome id '" +
                    biome.get<std::string>() + "'");
      }
    }
    if (catalog.contains("active_time"))
    {
      const std::string value = catalog.value("active_time", "any");
      Require(value == "day" || value == "night" || value == "any",
              species + ": bad active_time '" + value + "'");
    }
  }
  Require(naturalSpecies >= 50,
          "expected the full natural catalog (got " +
              std::to_string(naturalSpecies) + " species)");
}

} // namespace

int main()
{
  TestActiveTimeRules();
  TestBiomeGate();
  TestCatalogData();
  if (gFailures != 0)
  {
    std::cerr << gFailures << " spawn zone failure(s)\n";
    return 1;
  }
  std::cout << "creature spawn zones OK\n";
  return 0;
}
