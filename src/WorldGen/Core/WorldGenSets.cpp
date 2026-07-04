#include "WorldGen/Core/WorldGenSets.h"
#include "WorldGen/Features/ObjectFeatureConfig.h"
#include "WorldGen/Sampling/BiomeRegistry.h"
#include <nlohmann/json.hpp>

namespace cutum
{

namespace
{

ObjectFeatureRule ToFeatureRule(const WorldGenObjectEntry &entry)
{
  ObjectFeatureRule rule;
  rule.ObjectName = entry.ObjectName;
  rule.Spacing = entry.Spacing;
  rule.Weight = entry.Weight;
  rule.ChancePerColumn = entry.ChancePerColumn;
  rule.SeedOffset = entry.SeedOffset;
  rule.PlacementYOffset = entry.PlacementYOffset;
  rule.Mode = entry.Mode;
  rule.Scatter = entry.Scatter;
  rule.Surface = entry.Surface;
  for (const auto &b : entry.Biomes)
  {
    rule.Biomes.push_back(BiomeIdFromString(b));
  }
  for (const auto &s : entry.SubBiomes)
  {
    rule.SubBiomes.push_back(SubBiomeIdFromString(s));
  }
  return rule;
}

WorldGenObjectEntry FromFeatureRule(const ObjectFeatureRule &rule)
{
  WorldGenObjectEntry entry;
  entry.ObjectName = rule.ObjectName;
  entry.Spacing = rule.Spacing;
  entry.Weight = rule.Weight;
  entry.ChancePerColumn = rule.ChancePerColumn;
  entry.SeedOffset = rule.SeedOffset;
  entry.PlacementYOffset = rule.PlacementYOffset;
  entry.Mode = rule.Mode;
  entry.Scatter = rule.Scatter;
  entry.Surface = rule.Surface;
  for (BiomeId b : rule.Biomes)
  {
    entry.Biomes.push_back(BiomeIdToString(b));
  }
  for (SubBiomeId s : rule.SubBiomes)
  {
    entry.SubBiomes.push_back(SubBiomeIdToString(s));
  }
  return entry;
}

void ParseObjectPool(const nlohmann::json &pool,
                     std::unordered_map<std::string, WorldGenObjectSection> &out)
{
  if (!pool.is_object())
  {
    return;
  }
  for (auto it = pool.begin(); it != pool.end(); ++it)
  {
    WorldGenObjectSection section;
    if (!it.value().contains("entries") || !it.value()["entries"].is_array())
    {
      continue;
    }
    for (const auto &item : it.value()["entries"])
    {
      WorldGenObjectEntry entry;
      const std::string mode = item.value("mode", "object");
      if (mode == "scatter_blocks")
      {
        entry.Mode = ObjectFeatureRuleMode::ScatterBlocks;
        entry.Scatter.BlockName = item.value("block", "");
        entry.Scatter.Attempts = std::max(1, item.value("attempts", 4));
        entry.Scatter.Radius = std::max(0, item.value("radius", 2));
        entry.Scatter.DyOffset = item.value("dy_offset", 0);
        entry.Scatter.MaxPerChunk = std::max(0, item.value("max_per_chunk", 0));
      }
      else
      {
        entry.ObjectName = item.value("object", "");
      }
      entry.Spacing = item.value("spacing", 0);
      entry.Weight = std::max(1, item.value("weight", 1));
      entry.ChancePerColumn = item.value("chance_per_column", 0);
      entry.SeedOffset = item.value("seed_offset", 0);
      entry.PlacementYOffset = item.value("placement_y_offset", 0);
      if (item.contains("biomes") && item["biomes"].is_array())
      {
        for (const auto &b : item["biomes"])
        {
          if (b.is_string())
          {
            entry.Biomes.push_back(b.get<std::string>());
          }
        }
      }
      if (item.contains("sub_biomes") && item["sub_biomes"].is_array())
      {
        for (const auto &s : item["sub_biomes"])
        {
          if (s.is_string())
          {
            entry.SubBiomes.push_back(s.get<std::string>());
          }
        }
      }
      section.Entries.push_back(std::move(entry));
    }
    out[it.key()] = std::move(section);
  }
}

void AppendPool(std::vector<ObjectFeatureRule> &out,
                const std::unordered_map<std::string, WorldGenObjectSection> &sections)
{
  for (const auto &pair : sections)
  {
    for (const auto &entry : pair.second.Entries)
    {
      if (entry.Mode == ObjectFeatureRuleMode::ScatterBlocks)
      {
        if (!entry.Scatter.BlockName.empty())
        {
          out.push_back(ToFeatureRule(entry));
        }
      }
      else if (!entry.ObjectName.empty())
      {
        out.push_back(ToFeatureRule(entry));
      }
    }
  }
}

} // namespace

bool ParseWorldGenSets(const nlohmann::json &root, WorldGenSets &out,
                       std::string &error)
{
  out = WorldGenSets{};
  if (!root.is_object())
  {
    error = "worldgen_sets must be an object";
    return false;
  }
  out.SchemaVersion = root.value("schema_version", 0);
  if (out.SchemaVersion != 1)
  {
    error = "unsupported worldgen_sets schema_version";
    return false;
  }
  if (root.contains("objects") && root["objects"].is_object())
  {
    const auto &objects = root["objects"];
    ParseObjectPool(objects.value("vegetation", nlohmann::json::object()),
                    out.VegetationSections);
    ParseObjectPool(objects.value("ground_cover", nlohmann::json::object()),
                    out.GroundCoverSections);
    ParseObjectPool(objects.value("decoration", nlohmann::json::object()),
                    out.DecorationSections);
    ParseObjectPool(objects.value("structures", nlohmann::json::object()),
                    out.StructureSections);
  }
  if (root.contains("terrain") && root["terrain"].is_object())
  {
    for (auto it = root["terrain"].begin(); it != root["terrain"].end(); ++it)
    {
      WorldGenTerrainSlot slot;
      slot.Block = it.value().value("block", "");
      out.Terrain[it.key()] = slot;
    }
  }
  if (root.contains("ores") && root["ores"].is_object())
  {
    for (auto it = root["ores"].begin(); it != root["ores"].end(); ++it)
    {
      WorldGenOreSlot slot;
      slot.Enabled = it.value().value("enabled", true);
      slot.DensityMultiplier = it.value().value("density_multiplier", 1.0f);
      out.Ores[it.key()] = slot;
    }
  }
  return true;
}

void WriteWorldGenSets(nlohmann::json &root, const WorldGenSets &sets)
{
  nlohmann::json wg;
  wg["schema_version"] = sets.SchemaVersion;
  auto writePool =
      [](const std::unordered_map<std::string, WorldGenObjectSection> &sections)
  {
    nlohmann::json pool = nlohmann::json::object();
    for (const auto &pair : sections)
    {
      nlohmann::json section;
      nlohmann::json entries = nlohmann::json::array();
      for (const auto &entry : pair.second.Entries)
      {
        nlohmann::json item;
        if (entry.Mode == ObjectFeatureRuleMode::ScatterBlocks)
        {
          item["mode"] = "scatter_blocks";
          item["block"] = entry.Scatter.BlockName;
          item["attempts"] = entry.Scatter.Attempts;
          item["radius"] = entry.Scatter.Radius;
        }
        else
        {
          item["object"] = entry.ObjectName;
        }
        item["weight"] = entry.Weight;
        item["spacing"] = entry.Spacing;
        item["seed_offset"] = entry.SeedOffset;
        item["placement_y_offset"] = entry.PlacementYOffset;
        item["biomes"] = entry.Biomes;
        if (!entry.SubBiomes.empty())
        {
          item["sub_biomes"] = entry.SubBiomes;
        }
        entries.push_back(std::move(item));
      }
      section["entries"] = std::move(entries);
      pool[pair.first] = std::move(section);
    }
    return pool;
  };
  wg["objects"] = {{"vegetation", writePool(sets.VegetationSections)},
                   {"ground_cover", writePool(sets.GroundCoverSections)},
                   {"decoration", writePool(sets.DecorationSections)},
                   {"structures", writePool(sets.StructureSections)}};
  nlohmann::json terrain = nlohmann::json::object();
  for (const auto &pair : sets.Terrain)
  {
    terrain[pair.first] = {{"block", pair.second.Block}};
  }
  wg["terrain"] = std::move(terrain);
  nlohmann::json ores = nlohmann::json::object();
  for (const auto &pair : sets.Ores)
  {
    ores[pair.first] = {{"enabled", pair.second.Enabled},
                      {"density_multiplier", pair.second.DensityMultiplier}};
  }
  wg["ores"] = std::move(ores);
  root["worldgen_sets"] = std::move(wg);
}

WorldGenSets BuildDefaultWorldGenSets()
{
  WorldGenSets sets;
  sets.SchemaVersion = 1;
  if (!UObjectFeatureConfigStorage::IsLoaded())
  {
    return sets;
  }
  const ObjectFeatureConfig &cfg = UObjectFeatureConfigStorage::Get();
  auto bucket = [](const std::vector<ObjectFeatureRule> &rules)
  {
    WorldGenObjectSection section;
    for (const auto &rule : rules)
    {
      section.Entries.push_back(FromFeatureRule(rule));
    }
    return section;
  };
  sets.VegetationSections["default"] = bucket(cfg.Vegetation);
  sets.GroundCoverSections["default"] = bucket(cfg.GroundCover);
  sets.DecorationSections["default"] = bucket(cfg.Decoration);
  sets.StructureSections["default"] = bucket(cfg.Structures);
  return sets;
}

ObjectFeatureConfig ResolveObjectFeatures(const WorldGenSets &sets,
                                          const ObjectFeatureConfig &global)
{
  ObjectFeatureConfig resolved = global;
  if (!sets.VegetationSections.empty())
  {
    resolved.Vegetation.clear();
    AppendPool(resolved.Vegetation, sets.VegetationSections);
  }
  if (!sets.GroundCoverSections.empty())
  {
    resolved.GroundCover.clear();
    AppendPool(resolved.GroundCover, sets.GroundCoverSections);
  }
  if (!sets.DecorationSections.empty())
  {
    resolved.Decoration.clear();
    AppendPool(resolved.Decoration, sets.DecorationSections);
  }
  if (!sets.StructureSections.empty())
  {
    resolved.Structures.clear();
    AppendPool(resolved.Structures, sets.StructureSections);
  }
  return resolved;
}

bool ValidateWorldGenSets(const WorldGenSets &sets, std::string &error)
{
  if (sets.SchemaVersion != 1)
  {
    error = "worldgen_sets schema_version must be 1";
    return false;
  }
  (void)sets;
  return true;
}

} // namespace cutum
