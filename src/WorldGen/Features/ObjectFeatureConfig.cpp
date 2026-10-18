#include "WorldGen/Features/ObjectFeatureConfig.h"
#include "WorldGen/Core/WorldGenContentPin.h"
#include "WorldGen/Core/WorldGenContentPinTls.h"
#include "WorldGen/Sampling/BiomeRegistry.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <fstream>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>

namespace cutum
{

std::shared_ptr<const ObjectFeatureConfig> UObjectFeatureConfigStorage::Active =
    std::make_shared<ObjectFeatureConfig>();

namespace
{

std::unordered_map<std::string, ObjectFeatureRuleMode> &FeatureTypeMap()
{
  static std::unordered_map<std::string, ObjectFeatureRuleMode> types = {
      {"object", ObjectFeatureRuleMode::Object},
      {"scatter_blocks", ObjectFeatureRuleMode::ScatterBlocks},
      {"procedural_tree", ObjectFeatureRuleMode::ProceduralTree}};
  return types;
}

std::mutex &FeatureTypeMutex()
{
  static std::mutex mutex;
  return mutex;
}

std::unordered_map<ObjectFeatureRuleMode,
                   std::shared_ptr<const IObjectFeaturePlacementStrategy>> &
PlacementStrategies()
{
  static std::unordered_map<
      ObjectFeatureRuleMode,
      std::shared_ptr<const IObjectFeaturePlacementStrategy>> strategies;
  return strategies;
}

std::mutex &PlacementStrategyMutex()
{
  static std::mutex mutex;
  return mutex;
}

std::unordered_map<CaveFeatureKind,
                   std::shared_ptr<const ICaveFeaturePlacementStrategy>> &
CavePlacementStrategies()
{
  static std::unordered_map<
      CaveFeatureKind,
      std::shared_ptr<const ICaveFeaturePlacementStrategy>> strategies;
  return strategies;
}

std::mutex &CavePlacementStrategyMutex()
{
  static std::mutex mutex;
  return mutex;
}

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
        const std::string key = b.get<std::string>();
        rule.BiomeKeys.push_back(key);
        const BiomeId legacy = ParseBiome(key);
        if (legacy != BiomeId::Invalid)
        {
          rule.Biomes.push_back(legacy);
        }
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

void ParseEcologyProfile(const nlohmann::json &item, ObjectFeatureRule &rule)
{
  if (!item.contains("ecology") || !item["ecology"].is_object())
  {
    return;
  }
  const nlohmann::json &ecology = item["ecology"];
  const auto parse_axis = [&](const char *name, float &min_value,
                              float &max_value, bool &enabled)
  {
    if (!ecology.contains(name))
    {
      return;
    }
    const nlohmann::json &range = ecology[name];
    if (!range.is_array() || range.size() != 2 || !range[0].is_number() ||
        !range[1].is_number())
    {
      return;
    }
    min_value = range[0].get<float>();
    max_value = range[1].get<float>();
    enabled = std::isfinite(min_value) && std::isfinite(max_value) &&
              min_value >= 0.0f && max_value <= 1.0f &&
              min_value < max_value;
  };
  parse_axis("temperature", rule.Ecology.TemperatureMin,
             rule.Ecology.TemperatureMax, rule.Ecology.HasTemperatureRange);
  parse_axis("moisture", rule.Ecology.MoistureMin, rule.Ecology.MoistureMax,
             rule.Ecology.HasMoistureRange);
  if (ecology.contains("elevation") && ecology["elevation"].is_array() &&
      ecology["elevation"].size() == 2 && ecology["elevation"][0].is_number() &&
      ecology["elevation"][1].is_number())
  {
    rule.Ecology.MinElevation = ecology["elevation"][0].get<int>();
    rule.Ecology.MaxElevation = ecology["elevation"][1].get<int>();
    if (rule.Ecology.MinElevation > rule.Ecology.MaxElevation)
    {
      rule.Ecology.MinElevation = std::numeric_limits<int>::min();
      rule.Ecology.MaxElevation = std::numeric_limits<int>::max();
    }
  }
  if (ecology.contains("max_slope") && ecology["max_slope"].is_number())
  {
    rule.Ecology.MaxSlope = std::clamp(
        ecology["max_slope"].get<float>(), 0.0f, 64.0f);
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
    const std::string modeName = item.value("mode", "object");
    const std::optional<ObjectFeatureRuleMode> mode =
        ObjectFeatureTypeRegistry::Find(modeName);
    if (!mode)
    {
      std::cerr << "ObjectFeatureConfig: unknown feature type '" << modeName
                << "'\n";
      continue;
    }
    rule.Mode = *mode;
    if (rule.Mode == ObjectFeatureRuleMode::ScatterBlocks)
    {
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
    else if (rule.Mode == ObjectFeatureRuleMode::ProceduralTree)
    {
      rule.ObjectName = item.value("species", item.value("object", ""));
      if (rule.ObjectName.empty())
      {
        continue;
      }
      if (item.contains("tree_profile"))
      {
        const nlohmann::json &profile = item["tree_profile"];
        if (!profile.is_object())
        {
          continue;
        }
        rule.Tree.HeightMin = std::clamp(profile.value("height_min", 5), 2, 32);
        rule.Tree.HeightMax = std::clamp(profile.value("height_max", 8),
                                         rule.Tree.HeightMin, 40);
        rule.Tree.CrownRadiusX =
            std::clamp(profile.value("crown_radius_x", 3), 1, 8);
        rule.Tree.CrownRadiusZ =
            std::clamp(profile.value("crown_radius_z", 3), 1, 8);
        rule.Tree.CrownHeight =
            std::clamp(profile.value("crown_height", 4), 2, 16);
        rule.Tree.BranchCount =
            std::clamp(profile.value("branch_count", 2), 0, 8);
        rule.Tree.LeafDensity =
            std::clamp(profile.value("leaf_density", 0.84f), 0.2f, 1.0f);
        rule.Tree.RootChance =
            std::clamp(profile.value("root_chance", 0.45f), 0.0f, 1.0f);
        rule.Tree.TrunkBlock =
            profile.value("trunk_block", std::string("tree_log"));
        rule.Tree.LeavesBlock =
            profile.value("leaves_block", std::string("tree_leaves"));
        const std::string crown = profile.value("crown_shape", "round");
        if (crown == "conical")
        {
          rule.Tree.CrownShape = TreeCrownShape::Conical;
        }
        else if (crown == "umbrella")
        {
          rule.Tree.CrownShape = TreeCrownShape::Umbrella;
        }
        else if (crown == "columnar")
        {
          rule.Tree.CrownShape = TreeCrownShape::Columnar;
        }
        else
        {
          rule.Tree.CrownShape = TreeCrownShape::Round;
        }
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
    ParseEcologyProfile(item, rule);
    if (rule.BiomeKeys.empty())
    {
      continue;
    }
    out.push_back(std::move(rule));
  }
  return true;
}

void ParseCaveFeatureArray(const nlohmann::json &arr,
                           std::vector<CaveFeatureRule> &out)
{
  if (!arr.is_array())
  {
    return;
  }
  for (const auto &item : arr)
  {
    if (!item.is_object())
    {
      continue;
    }
    CaveFeatureRule rule;
    const std::string biome = item.value("biome", "");
    if (biome.empty())
    {
      continue;
    }
    rule.BiomeKeyName = biome;
    const BiomeId legacyBiome = BiomeIdFromString(biome);
    if (legacyBiome != BiomeId::Invalid)
    {
      rule.Biome = legacyBiome;
    }

    const std::string kind = item.value("kind", "");
    if (kind == "moss_patch")
    {
      rule.Kind = CaveFeatureKind::MossPatch;
      rule.BlockName = item.value("block", "moss_block");
    }
    else if (kind == "stalagmite")
    {
      rule.Kind = CaveFeatureKind::Stalagmite;
      rule.BlockName = item.value("block", "tuff");
    }
    else if (kind == "amethyst_cluster")
    {
      rule.Kind = CaveFeatureKind::AmethystCluster;
      rule.BlockName = item.value("block", "amethyst_block");
    }
    else
    {
      continue;
    }

    rule.Chance = std::max(1, item.value("chance", rule.Chance));
    rule.Radius = std::clamp(item.value("radius", rule.Radius), 0, 4);
    rule.MaxHeight = std::clamp(item.value("max_height", rule.MaxHeight), 1, 6);
    rule.SeedOffset = item.value("seed_offset", 0U);
    if (!rule.BlockName.empty())
    {
      out.push_back(std::move(rule));
    }
  }
}

} // namespace

bool ObjectFeatureTypeRegistry::Register(const std::string &name,
                                         ObjectFeatureRuleMode mode)
{
  if (name.empty())
  {
    return false;
  }
  std::lock_guard<std::mutex> lock(FeatureTypeMutex());
  return FeatureTypeMap().emplace(name, mode).second;
}

std::optional<ObjectFeatureRuleMode>
ObjectFeatureTypeRegistry::Find(const std::string &name)
{
  std::lock_guard<std::mutex> lock(FeatureTypeMutex());
  const auto it = FeatureTypeMap().find(name);
  return it == FeatureTypeMap().end()
             ? std::optional<ObjectFeatureRuleMode>{}
             : std::optional<ObjectFeatureRuleMode>{it->second};
}

bool ObjectFeaturePlacementStrategyRegistry::Register(
    ObjectFeatureRuleMode mode,
    std::shared_ptr<const IObjectFeaturePlacementStrategy> strategy)
{
  if (!strategy)
  {
    return false;
  }
  std::lock_guard<std::mutex> lock(PlacementStrategyMutex());
  return PlacementStrategies().emplace(mode, std::move(strategy)).second;
}

std::shared_ptr<const IObjectFeaturePlacementStrategy>
ObjectFeaturePlacementStrategyRegistry::Find(ObjectFeatureRuleMode mode)
{
  std::lock_guard<std::mutex> lock(PlacementStrategyMutex());
  const auto it = PlacementStrategies().find(mode);
  return it == PlacementStrategies().end() ? nullptr : it->second;
}

bool CaveFeaturePlacementStrategyRegistry::Register(
    CaveFeatureKind kind,
    std::shared_ptr<const ICaveFeaturePlacementStrategy> strategy)
{
  if (!strategy)
  {
    return false;
  }
  std::lock_guard<std::mutex> lock(CavePlacementStrategyMutex());
  return CavePlacementStrategies().emplace(kind, std::move(strategy)).second;
}

std::shared_ptr<const ICaveFeaturePlacementStrategy>
CaveFeaturePlacementStrategyRegistry::Find(CaveFeatureKind kind)
{
  std::lock_guard<std::mutex> lock(CavePlacementStrategyMutex());
  const auto it = CavePlacementStrategies().find(kind);
  return it == CavePlacementStrategies().end() ? nullptr : it->second;
}

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
  if (name == "underwater_floor")
  {
    return SurfaceConstraintKind::UnderwaterFloor;
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
  case SurfaceConstraintKind::UnderwaterFloor:
    return "underwater_floor";
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
  auto next = std::make_shared<ObjectFeatureConfig>();
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
                   next->Vegetation);
    ParseRuleArray(root.value("ground_cover", nlohmann::json::array()),
                   next->GroundCover);
    ParseRuleArray(root.value("decoration", nlohmann::json::array()),
                   next->Decoration);
    ParseRuleArray(root.value("structures", nlohmann::json::array()),
                   next->Structures);
    ParseCaveFeatureArray(root.value("cave_features", nlohmann::json::array()),
                          next->CaveFeatures);
    if (root.contains("structure_placement") && root["structure_placement"].is_object())
    {
      const auto &sp = root["structure_placement"];
      next->structureCellSize = std::max(16, sp.value("cell_size", 64));
      next->structureChancePerCell = std::max(1, sp.value("chance_per_cell", 12));
      next->structureMinSpacing = std::max(32, sp.value("min_spacing", 128));
    }
  }
  catch (const nlohmann::json::exception &e)
  {
    std::cerr << "ObjectFeatureConfig: parse error " << path << ": " << e.what()
              << std::endl;
    return false;
  }
  const size_t rule_count = next->Vegetation.size() + next->GroundCover.size() +
                            next->Decoration.size() + next->Structures.size() +
                            next->CaveFeatures.size();
  std::atomic_store_explicit(
      &Active, std::shared_ptr<const ObjectFeatureConfig>(std::move(next)),
      std::memory_order_release);
  std::cout << "ObjectFeatureConfig: loaded " << rule_count << " rules from "
            << path << std::endl;
  return true;
}

std::shared_ptr<const ObjectFeatureConfig>
UObjectFeatureConfigStorage::GetSnapshot()
{
  return std::atomic_load_explicit(&Active, std::memory_order_acquire);
}

const ObjectFeatureConfig &UObjectFeatureConfigStorage::Get()
{
  if (const WorldGenContentSnapshot *pin = GetPinnedWorldGenContent())
  {
    if (pin->Features)
    {
      return *pin->Features;
    }
  }
  thread_local std::shared_ptr<const ObjectFeatureConfig> keep;
  keep = GetSnapshot();
  return *keep;
}

bool UObjectFeatureConfigStorage::IsLoaded()
{
  const auto snap = GetSnapshot();
  if (!snap)
  {
    return false;
  }
  return !(snap->Vegetation.empty() && snap->GroundCover.empty() &&
           snap->Decoration.empty() && snap->Structures.empty() &&
           snap->CaveFeatures.empty());
}

} // namespace cutum
