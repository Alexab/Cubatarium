#include "WorldGen/Features/PrefabFeatureConfig.h"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace cutum
{

PrefabFeatureConfig UPrefabFeatureConfigStorage::Config{};
bool UPrefabFeatureConfigStorage::Loaded = false;

namespace
{

BiomeId ParseBiome(const std::string &name)
{
  return BiomeIdFromString(name);
}

bool ParseRuleArray(const nlohmann::json &arr,
                    std::vector<PrefabFeatureRule> &out)
{
  if (!arr.is_array())
  {
    return false;
  }
  for (const auto &item : arr)
  {
    if (!item.is_object())
    {
      continue;
    }
    PrefabFeatureRule rule;
    rule.PrefabName = item.value("prefab", "");
    if (rule.PrefabName.empty())
    {
      continue;
    }
    rule.Spacing = item.value("spacing", 0);
    rule.Weight = std::max(1, item.value("weight", 1));
    rule.ChancePerColumn = item.value("chance_per_column", 0);
    rule.SeedOffset = item.value("seed_offset", 0);
    rule.PlacementYOffset = item.value("placement_y_offset", 0);
    if (item.contains("biomes") && item["biomes"].is_array())
    {
      for (const auto &b : item["biomes"])
      {
        if (b.is_string())
        {
          rule.Biomes.push_back(ParseBiome(b.get<std::string>()));
        }
      }
    }
    if (item.contains("sub_biomes") && item["sub_biomes"].is_array())
    {
      for (const auto &sub : item["sub_biomes"])
      {
        if (sub.is_string())
        {
          rule.SubBiomes.push_back(SubBiomeIdFromString(sub.get<std::string>()));
        }
      }
    }
    if (rule.Biomes.empty())
    {
      continue;
    }
    out.push_back(std::move(rule));
  }
  return true;
}

} // namespace

BiomeId BiomeIdFromString(const std::string &name)
{
  if (name == "forest")
  {
    return BiomeId::Forest;
  }
  if (name == "desert")
  {
    return BiomeId::Desert;
  }
  if (name == "hills")
  {
    return BiomeId::Hills;
  }
  if (name == "tundra")
  {
    return BiomeId::Tundra;
  }
  return BiomeId::Plains;
}

const char *BiomeIdToString(BiomeId biome)
{
  switch (biome)
  {
  case BiomeId::Forest:
    return "forest";
  case BiomeId::Desert:
    return "desert";
  case BiomeId::Hills:
    return "hills";
  case BiomeId::Tundra:
    return "tundra";
  case BiomeId::Plains:
  default:
    return "plains";
  }
}

SubBiomeId SubBiomeIdFromString(const std::string &name)
{
  if (name == "woodland")
  {
    return SubBiomeId::Woodland;
  }
  if (name == "dense_forest")
  {
    return SubBiomeId::DenseForest;
  }
  if (name == "sparse_forest")
  {
    return SubBiomeId::SparseForest;
  }
  if (name == "scrub_desert")
  {
    return SubBiomeId::ScrubDesert;
  }
  if (name == "dunes")
  {
    return SubBiomeId::Dunes;
  }
  return SubBiomeId::Default;
}

const char *SubBiomeIdToString(SubBiomeId subBiome)
{
  switch (subBiome)
  {
  case SubBiomeId::Woodland:
    return "woodland";
  case SubBiomeId::DenseForest:
    return "dense_forest";
  case SubBiomeId::SparseForest:
    return "sparse_forest";
  case SubBiomeId::ScrubDesert:
    return "scrub_desert";
  case SubBiomeId::Dunes:
    return "dunes";
  case SubBiomeId::Default:
  default:
    return "default";
  }
}

bool UPrefabFeatureConfigStorage::LoadFromFile(
    const std::filesystem::path &path)
{
  Loaded = false;
  Config = PrefabFeatureConfig{};
  std::ifstream in(path);
  if (!in.is_open())
  {
    std::cerr << "PrefabFeatureConfig: could not open " << path << std::endl;
    return false;
  }
  try
  {
    const nlohmann::json root = nlohmann::json::parse(in);
    ParseRuleArray(root.value("vegetation", nlohmann::json::array()),
                   Config.Vegetation);
    ParseRuleArray(root.value("decoration", nlohmann::json::array()),
                   Config.Decoration);
    ParseRuleArray(root.value("structures", nlohmann::json::array()),
                   Config.Structures);
  }
  catch (const nlohmann::json::exception &e)
  {
    std::cerr << "PrefabFeatureConfig: parse error " << path << ": " << e.what()
              << std::endl;
    return false;
  }
  Loaded = true;
  std::cout << "PrefabFeatureConfig: loaded "
            << Config.Vegetation.size() + Config.Decoration.size() +
                   Config.Structures.size()
            << " rules from " << path << std::endl;
  return true;
}

const PrefabFeatureConfig &UPrefabFeatureConfigStorage::Get() { return Config; }

bool UPrefabFeatureConfigStorage::IsLoaded() { return Loaded; }

} // namespace cutum
