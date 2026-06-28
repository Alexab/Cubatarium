#include "WorldGen/Features/ObjectFeatureConfig.h"
#include "WorldGen/Sampling/BiomeRegistry.h"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace cutum
{

ObjectFeatureConfig UObjectFeatureConfigStorage::Config{};
bool UObjectFeatureConfigStorage::Loaded = false;

namespace
{

BiomeId ParseBiome(const std::string &name)
{
  return BiomeIdFromString(name);
}

void ParseBiomeArray(const nlohmann::json &item, ObjectFeatureRule &rule)
{
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
}

void ParseSubBiomeArray(const nlohmann::json &item, ObjectFeatureRule &rule)
{
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
}

bool ParseRuleArray(const nlohmann::json &arr,
                    std::vector<ObjectFeatureRule> &out)
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
    ObjectFeatureRule rule;
    const std::string mode = item.value("mode", "object");
    if (mode == "scatter_blocks")
    {
      rule.Mode = ObjectPlacementMode::ScatterBlocks;
      rule.Scatter.BlockName = item.value("block", "");
      rule.Scatter.Attempts = std::max(1, item.value("attempts", 4));
      rule.Scatter.Radius = std::max(0, item.value("radius", 2));
      rule.Scatter.DyOffset = item.value("dy_offset", 0);
      rule.Scatter.MaxPerChunk = std::max(0, item.value("max_per_chunk", 0));
      if (rule.Scatter.BlockName.empty())
      {
        continue;
      }
    }
    else
    {
      rule.ObjectName = item.value("object", "");
      if (rule.ObjectName.empty())
      {
        continue;
      }
    }
    rule.Spacing = item.value("spacing", 0);
    rule.Weight = std::max(1, item.value("weight", 1));
    rule.ChancePerColumn = item.value("chance_per_column", 0);
    rule.SeedOffset = item.value("seed_offset", 0);
    rule.PlacementYOffset = item.value("placement_y_offset", 0);
    if (item.contains("surface_constraint") && item["surface_constraint"].is_string())
    {
      rule.Surface.Kind = SurfaceConstraintKindFromString(
          item["surface_constraint"].get<std::string>());
    }
    if (item.contains("near_water_radius"))
    {
      rule.Surface.NearWaterRadius =
          std::max(1, item["near_water_radius"].get<int>());
    }
    ParseBiomeArray(item, rule);
    ParseSubBiomeArray(item, rule);
    if (rule.Biomes.empty())
    {
      continue;
    }
    out.push_back(std::move(rule));
  }
  return true;
}

} // namespace

SurfaceConstraintKind SurfaceConstraintKindFromString(const std::string &name)
{
  if (name == "grass")
  {
    return SurfaceConstraintKind::Grass;
  }
  if (name == "near_water")
  {
    return SurfaceConstraintKind::NearWater;
  }
  if (name == "water_surface")
  {
    return SurfaceConstraintKind::WaterSurface;
  }
  return SurfaceConstraintKind::AnyLand;
}

const char *SurfaceConstraintKindToString(SurfaceConstraintKind kind)
{
  switch (kind)
  {
  case SurfaceConstraintKind::Grass:
    return "grass";
  case SurfaceConstraintKind::NearWater:
    return "near_water";
  case SurfaceConstraintKind::WaterSurface:
    return "water_surface";
  case SurfaceConstraintKind::AnyLand:
  default:
    return "any_land";
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
  if (name == "dry")
  {
    return SubBiomeId::SavannaDry;
  }
  if (name == "wet")
  {
    return SubBiomeId::SavannaWet;
  }
  if (name == "scrub_dry")
  {
    return SubBiomeId::ScrubDry;
  }
  if (name == "scrub_wet")
  {
    return SubBiomeId::ScrubWet;
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
  case SubBiomeId::SavannaDry:
    return "dry";
  case SubBiomeId::SavannaWet:
    return "wet";
  case SubBiomeId::ScrubDry:
    return "scrub_dry";
  case SubBiomeId::ScrubWet:
    return "scrub_wet";
  case SubBiomeId::Default:
  default:
    return "default";
  }
}

bool UObjectFeatureConfigStorage::LoadFromFile(
    const std::filesystem::path &path)
{
  Loaded = false;
  Config = ObjectFeatureConfig{};
  std::ifstream in(path);
  if (!in.is_open())
  {
    std::cerr << "ObjectFeatureConfig: could not open " << path << std::endl;
    return false;
  }
  try
  {
    const nlohmann::json root = nlohmann::json::parse(in);
    ParseRuleArray(root.value("vegetation", nlohmann::json::array()),
                   Config.Vegetation);
    ParseRuleArray(root.value("ground_cover", nlohmann::json::array()),
                   Config.GroundCover);
    ParseRuleArray(root.value("decoration", nlohmann::json::array()),
                   Config.Decoration);
    ParseRuleArray(root.value("structures", nlohmann::json::array()),
                   Config.Structures);
    if (root.contains("structure_placement") && root["structure_placement"].is_object())
    {
      const auto &sp = root["structure_placement"];
      Config.structureCellSize = std::max(16, sp.value("cell_size", 64));
      Config.structureChancePerCell = std::max(1, sp.value("chance_per_cell", 12));
      Config.structureMinSpacing = std::max(32, sp.value("min_spacing", 128));
    }
  }
  catch (const nlohmann::json::exception &e)
  {
    std::cerr << "ObjectFeatureConfig: parse error " << path << ": " << e.what()
              << std::endl;
    return false;
  }
  Loaded = true;
  std::cout << "ObjectFeatureConfig: loaded "
            << Config.Vegetation.size() + Config.GroundCover.size() +
                   Config.Decoration.size() +
                   Config.Structures.size()
            << " rules from " << path << std::endl;
  return true;
}

const ObjectFeatureConfig &UObjectFeatureConfigStorage::Get() { return Config; }

bool UObjectFeatureConfigStorage::IsLoaded() { return Loaded; }

} // namespace cutum
