#include "Audio/MaterialSoundMap.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace cutum
{

namespace
{

using json = nlohmann::json;

std::vector<std::string> ReadStringArray(const json &node)
{
  std::vector<std::string> out;
  if (node.is_array())
  {
    for (const auto &item : node)
    {
      if (item.is_string())
      {
        out.push_back(item.get<std::string>());
      }
    }
  }
  return out;
}

} // namespace

bool UMaterialSoundMap::LoadFromFile(const std::string &path)
{
  std::ifstream file(path);
  if (!file.is_open())
  {
    std::cerr << "Audio: material sound map not found: " << path << std::endl;
    return false;
  }
  try
  {
    json root = json::parse(file);
    return Load(&root);
  }
  catch (const json::exception &e)
  {
    std::cerr << "Audio: material sound map parse error in " << path << ": "
              << e.what() << std::endl;
    return false;
  }
}

bool UMaterialSoundMap::Load(const void *jsonRoot)
{
  const json *root = static_cast<const json *>(jsonRoot);
  if (!root || !root->is_object())
  {
    std::cerr << "Audio: material sound map is not an object" << std::endl;
    return false;
  }

  if (root->contains("surface_materials") &&
      (*root)["surface_materials"].is_object())
  {
    Materials.clear();
    for (auto it = (*root)["surface_materials"].begin();
         it != (*root)["surface_materials"].end(); ++it)
    {
      if (!it.value().is_object())
      {
        continue;
      }
      SurfaceMaterialDef def;
      def.Name = it.key();
      def.Blocks = ReadStringArray(it.value()["blocks"]);
      def.DigGroups = ReadStringArray(it.value()["dig_groups"]);
      def.FootstepSet = it.value().value("footstep_set", std::string());
      def.DigSet = it.value().value("dig_set", std::string());
      def.DugSet = it.value().value("dug_set", std::string());
      Materials.push_back(std::move(def));
    }
  }

  if (root->contains("creature_defaults") &&
      (*root)["creature_defaults"].is_object())
  {
    CreatureDefaults.clear();
    for (auto it = (*root)["creature_defaults"].begin();
         it != (*root)["creature_defaults"].end(); ++it)
    {
      CreatureSoundDefaultDef def;
      def.Archetype = it.key();
      if (it.value().is_string())
      {
        def.FootstepSet = it.value().get<std::string>();
      }
      else if (it.value().is_object())
      {
        def.FootstepSet = it.value().value("footstep_set", std::string());
        def.PitchMul = it.value().value("pitch", 1.f);
        def.IntervalMul = it.value().value("interval_mul", 1.f);
      }
      CreatureDefaults.push_back(std::move(def));
    }
  }

  if (root->contains("biomes") && (*root)["biomes"].is_object())
  {
    Biomes.clear();
    for (auto it = (*root)["biomes"].begin(); it != (*root)["biomes"].end();
         ++it)
    {
      if (!it.value().is_object())
      {
        continue;
      }
      BiomeAmbienceDef def;
      def.Biome = it.key();
      def.LoopSet = it.value().value("loop_set", std::string());
      def.DayStingers = ReadStringArray(it.value()["day_stingers"]);
      def.NightStingers = ReadStringArray(it.value()["night_stingers"]);
      Biomes.push_back(std::move(def));
    }
  }

  if (root->contains("fallback_biome") && (*root)["fallback_biome"].is_object())
  {
    const json &node = (*root)["fallback_biome"];
    FallbackBiome = BiomeAmbienceDef{};
    FallbackBiome.Biome = "fallback";
    FallbackBiome.LoopSet = node.value("loop_set", std::string());
    FallbackBiome.DayStingers = ReadStringArray(node["day_stingers"]);
    FallbackBiome.NightStingers = ReadStringArray(node["night_stingers"]);
  }

  if (root->contains("weather") && (*root)["weather"].is_object())
  {
    Weathers.clear();
    for (auto it = (*root)["weather"].begin(); it != (*root)["weather"].end();
         ++it)
    {
      if (!it.value().is_string())
      {
        continue;
      }
      WeatherAmbienceDef def;
      def.Weather = it.key();
      def.LoopSet = it.value().get<std::string>();
      Weathers.push_back(std::move(def));
    }
  }

  return true;
}

std::uint32_t
UMaterialSoundMap::ResolveMaterial(const std::string &blockName,
                                   const std::string &digGroupsCsv) const
{
  // Exact block name wins (longest match first so "dirt_with_grass" beats
  // "dirt" if both existed).
  for (const SurfaceMaterialDef &def : Materials)
  {
    for (const std::string &block : def.Blocks)
    {
      if (block == blockName)
      {
        return static_cast<std::uint32_t>(&def - Materials.data()) + 1;
      }
    }
  }
  // Luanti dig groups encode material semantics: cracky=stone,
  // crumbly=soft/soil, choppy=wood, snappy=leaf-like.
  static const char *const kGroupOrder[] = {"cracky", "crumbly", "choppy",
                                            "snappy"};
  for (const char *const group : kGroupOrder)
  {
    if (digGroupsCsv.find(group) == std::string::npos)
    {
      continue;
    }
    for (std::size_t i = 0; i < Materials.size(); ++i)
    {
      for (const std::string &defGroup : Materials[i].DigGroups)
      {
        if (defGroup == group)
        {
          return static_cast<std::uint32_t>(i) + 1;
        }
      }
    }
  }
  return 0;
}

const SurfaceMaterialDef *
UMaterialSoundMap::GetMaterial(std::uint32_t materialId) const
{
  if (materialId == 0 || materialId > Materials.size())
  {
    return nullptr;
  }
  return &Materials[materialId - 1];
}

const BiomeAmbienceDef *
UMaterialSoundMap::ResolveBiome(const std::string &biomeId) const
{
  // Sub-biome ids embed their family ("dense_forest" → "forest",
  // "dripstone_caves" → "caves"), so containment with the longest contained
  // id wins.
  const BiomeAmbienceDef *best = nullptr;
  std::size_t bestLen = 0;
  for (const BiomeAmbienceDef &def : Biomes)
  {
    if (!def.Biome.empty() && biomeId.find(def.Biome) != std::string::npos &&
        def.Biome.size() > bestLen)
    {
      best = &def;
      bestLen = def.Biome.size();
    }
  }
  return best ? best : &FallbackBiome;
}

const CreatureSoundDefaultDef *
UMaterialSoundMap::FindCreatureDefault(const std::string &archetype) const
{
  for (const CreatureSoundDefaultDef &def : CreatureDefaults)
  {
    if (def.Archetype == archetype)
    {
      return &def;
    }
  }
  return nullptr;
}

const WeatherAmbienceDef *
UMaterialSoundMap::FindWeather(const std::string &weatherName) const
{
  for (const WeatherAmbienceDef &def : Weathers)
  {
    if (def.Weather == weatherName)
    {
      return &def;
    }
  }
  return nullptr;
}

namespace
{

UMaterialSoundMap *LoadedMaterialMap = nullptr;

} // namespace

const UMaterialSoundMap *GetAudioMaterialMap() { return LoadedMaterialMap; }

bool LoadAudioMaterialMap(const std::string &path)
{
  if (LoadedMaterialMap)
  {
    return true;
  }
  auto map = std::make_unique<UMaterialSoundMap>();
  if (!map->LoadFromFile(path))
  {
    return false;
  }
  LoadedMaterialMap = map.release();
  return true;
}

} // namespace cutum
