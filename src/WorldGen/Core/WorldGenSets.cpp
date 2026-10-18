#include "WorldGen/Core/WorldGenSets.h"
#include "WorldGen/Features/ObjectFeatureConfig.h"
#include "WorldGen/Sampling/BiomeRegistry.h"
#include <algorithm>
#include <cmath>
#include <limits>
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
  rule.Tree = entry.Tree;
  rule.Shrub = entry.Shrub;
  rule.Ecology = entry.Ecology;
  for (const auto &b : entry.Biomes)
  {
    rule.BiomeKeys.push_back(b);
    const BiomeId biome = BiomeIdFromString(b);
    if (biome != BiomeId::Invalid)
    {
      rule.Biomes.push_back(biome);
    }
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
  entry.Tree = rule.Tree;
  entry.Shrub = rule.Shrub;
  entry.Ecology = rule.Ecology;
  if (!rule.BiomeKeys.empty())
  {
    entry.Biomes = rule.BiomeKeys;
  }
  else
  {
    for (BiomeId b : rule.Biomes)
    {
      if (b != BiomeId::Invalid)
      {
        entry.Biomes.push_back(BiomeIdToString(b));
      }
    }
  }
  for (SubBiomeId s : rule.SubBiomes)
  {
    entry.SubBiomes.push_back(SubBiomeIdToString(s));
  }
  return entry;
}

TreeCrownShape ParseTreeCrownShape(const std::string &name)
{
  if (name == "conical")
  {
    return TreeCrownShape::Conical;
  }
  if (name == "umbrella")
  {
    return TreeCrownShape::Umbrella;
  }
  if (name == "columnar")
  {
    return TreeCrownShape::Columnar;
  }
  return TreeCrownShape::Round;
}

const char *TreeCrownShapeName(TreeCrownShape shape)
{
  switch (shape)
  {
  case TreeCrownShape::Conical:
    return "conical";
  case TreeCrownShape::Umbrella:
    return "umbrella";
  case TreeCrownShape::Columnar:
    return "columnar";
  case TreeCrownShape::Round:
  default:
    return "round";
  }
}

void ParseTreeProfile(const nlohmann::json &profile, TreeSpeciesProfile &tree)
{
  if (!profile.is_object())
  {
    return;
  }
  tree.HeightMin = std::clamp(profile.value("height_min", 5), 2, 32);
  tree.HeightMax =
      std::clamp(profile.value("height_max", 8), tree.HeightMin, 40);
  tree.CrownRadiusX = std::clamp(profile.value("crown_radius_x", 3), 1, 8);
  tree.CrownRadiusZ = std::clamp(profile.value("crown_radius_z", 3), 1, 8);
  tree.CrownHeight = std::clamp(profile.value("crown_height", 4), 2, 16);
  tree.BranchCount = std::clamp(profile.value("branch_count", 2), 0, 8);
  tree.CrownLobes = std::clamp(profile.value("crown_lobes", 2), 0, 8);
  tree.LeafDensity =
      std::clamp(profile.value("leaf_density", 0.84f), 0.2f, 1.0f);
  tree.RootChance = std::clamp(profile.value("root_chance", 0.45f), 0.0f, 1.0f);
  tree.CrownIrregularity =
      std::clamp(profile.value("crown_irregularity", 0.18f), 0.0f, 0.45f);
  tree.TrunkBlock = profile.value("trunk_block", std::string("tree_log"));
  tree.LeavesBlock = profile.value("leaves_block", std::string("tree_leaves"));
  tree.CrownShape =
      ParseTreeCrownShape(profile.value("crown_shape", std::string("round")));
}

void ParseShrubProfile(const nlohmann::json &profile,
                       ShrubSpeciesProfile &shrub)
{
  if (!profile.is_object())
  {
    return;
  }
  shrub.HeightMin = std::clamp(profile.value("height_min", 2), 1, 8);
  shrub.HeightMax =
      std::clamp(profile.value("height_max", 3), shrub.HeightMin, 10);
  shrub.RadiusX = std::clamp(profile.value("radius_x", 2), 1, 6);
  shrub.RadiusZ = std::clamp(profile.value("radius_z", 2), 1, 6);
  shrub.LobeCount = std::clamp(profile.value("lobe_count", 4), 1, 10);
  shrub.LeafDensity =
      std::clamp(profile.value("leaf_density", 0.86f), 0.2f, 1.0f);
  shrub.CrownIrregularity =
      std::clamp(profile.value("crown_irregularity", 0.22f), 0.0f, 0.5f);
  shrub.AccentChance =
      std::clamp(profile.value("accent_chance", 0.0f), 0.0f, 1.0f);
  shrub.StemBlock = profile.value("stem_block", std::string("mtg_bush_stem"));
  shrub.LeavesBlock =
      profile.value("leaves_block", std::string("mtg_bush_leaves"));
  shrub.AccentBlock = profile.value("accent_block", std::string{});
}

void ParseEcology(const nlohmann::json &item, FeatureEcologyProfile &ecology)
{
  if (!item.contains("ecology") || !item["ecology"].is_object())
  {
    return;
  }
  const auto &value = item["ecology"];
  const auto parseRange =
      [&value](const char *key, float &minimum, float &maximum, bool &enabled)
  {
    if (!value.contains(key) || !value[key].is_array() ||
        value[key].size() != 2 || !value[key][0].is_number() ||
        !value[key][1].is_number())
    {
      return;
    }
    minimum = value[key][0].get<float>();
    maximum = value[key][1].get<float>();
    enabled = std::isfinite(minimum) && std::isfinite(maximum) &&
              minimum >= 0.0f && maximum <= 1.0f && minimum < maximum;
  };
  parseRange("temperature", ecology.TemperatureMin, ecology.TemperatureMax,
             ecology.HasTemperatureRange);
  parseRange("moisture", ecology.MoistureMin, ecology.MoistureMax,
             ecology.HasMoistureRange);
  if (value.contains("elevation") && value["elevation"].is_array() &&
      value["elevation"].size() == 2 && value["elevation"][0].is_number() &&
      value["elevation"][1].is_number())
  {
    ecology.MinElevation = value["elevation"][0].get<int>();
    ecology.MaxElevation = value["elevation"][1].get<int>();
    if (ecology.MinElevation > ecology.MaxElevation)
    {
      ecology.MinElevation = std::numeric_limits<int>::min();
      ecology.MaxElevation = std::numeric_limits<int>::max();
    }
  }
  if (value.contains("max_slope") && value["max_slope"].is_number())
  {
    ecology.MaxSlope = std::clamp(value["max_slope"].get<float>(), 0.0f, 64.0f);
  }
}

void ParsePlacementMetadata(const nlohmann::json &item,
                            WorldGenObjectEntry &entry)
{
  if (item.contains("surface_constraint") &&
      item["surface_constraint"].is_string())
  {
    entry.Surface.Kind = SurfaceConstraintKindFromString(
        item["surface_constraint"].get<std::string>());
  }
  entry.Surface.NearWaterRadius =
      std::max(1, item.value("near_water_radius", 5));
  ParseEcology(item, entry.Ecology);
}

nlohmann::json WriteTreeProfile(const TreeSpeciesProfile &tree)
{
  return {{"height_min", tree.HeightMin},
          {"height_max", tree.HeightMax},
          {"crown_radius_x", tree.CrownRadiusX},
          {"crown_radius_z", tree.CrownRadiusZ},
          {"crown_height", tree.CrownHeight},
          {"branch_count", tree.BranchCount},
          {"crown_lobes", tree.CrownLobes},
          {"leaf_density", tree.LeafDensity},
          {"root_chance", tree.RootChance},
          {"crown_irregularity", tree.CrownIrregularity},
          {"crown_shape", TreeCrownShapeName(tree.CrownShape)},
          {"trunk_block", tree.TrunkBlock},
          {"leaves_block", tree.LeavesBlock}};
}

nlohmann::json WriteShrubProfile(const ShrubSpeciesProfile &shrub)
{
  nlohmann::json profile = {{"height_min", shrub.HeightMin},
                            {"height_max", shrub.HeightMax},
                            {"radius_x", shrub.RadiusX},
                            {"radius_z", shrub.RadiusZ},
                            {"lobe_count", shrub.LobeCount},
                            {"leaf_density", shrub.LeafDensity},
                            {"crown_irregularity", shrub.CrownIrregularity},
                            {"accent_chance", shrub.AccentChance},
                            {"stem_block", shrub.StemBlock},
                            {"leaves_block", shrub.LeavesBlock}};
  if (!shrub.AccentBlock.empty())
  {
    profile["accent_block"] = shrub.AccentBlock;
  }
  return profile;
}

void WriteEcology(const FeatureEcologyProfile &ecology, nlohmann::json &item)
{
  nlohmann::json value = nlohmann::json::object();
  if (ecology.HasTemperatureRange)
  {
    value["temperature"] = {ecology.TemperatureMin, ecology.TemperatureMax};
  }
  if (ecology.HasMoistureRange)
  {
    value["moisture"] = {ecology.MoistureMin, ecology.MoistureMax};
  }
  if (ecology.MinElevation != std::numeric_limits<int>::min() ||
      ecology.MaxElevation != std::numeric_limits<int>::max())
  {
    value["elevation"] = {ecology.MinElevation, ecology.MaxElevation};
  }
  if (ecology.MaxSlope > 0.0f)
  {
    value["max_slope"] = ecology.MaxSlope;
  }
  if (!value.empty())
  {
    item["ecology"] = std::move(value);
  }
}

void ParseObjectPool(
    const nlohmann::json &pool,
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
      const std::string modeName = item.value("mode", "object");
      const auto mode = ObjectFeatureTypeRegistry::Find(modeName);
      if (!mode)
      {
        continue;
      }
      entry.Mode = *mode;
      if (entry.Mode == ObjectFeatureRuleMode::ScatterBlocks)
      {
        entry.Scatter.BlockName = item.value("block", "");
        entry.Scatter.Attempts = std::max(1, item.value("attempts", 4));
        entry.Scatter.Radius = std::max(0, item.value("radius", 2));
        entry.Scatter.DyOffset = item.value("dy_offset", 0);
        entry.Scatter.MaxPerChunk = std::max(0, item.value("max_per_chunk", 0));
      }
      else if (entry.Mode == ObjectFeatureRuleMode::ProceduralTree)
      {
        entry.ObjectName = item.value("species", item.value("object", ""));
        if (item.contains("tree_profile"))
        {
          ParseTreeProfile(item["tree_profile"], entry.Tree);
        }
      }
      else if (entry.Mode == ObjectFeatureRuleMode::ProceduralShrub)
      {
        entry.ObjectName = item.value("species", item.value("object", ""));
        if (item.contains("shrub_profile"))
        {
          ParseShrubProfile(item["shrub_profile"], entry.Shrub);
        }
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
      ParsePlacementMetadata(item, entry);
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
      if (entry.Mode == ObjectFeatureRuleMode::ScatterBlocks
              ? !entry.Scatter.BlockName.empty()
              : !entry.ObjectName.empty())
      {
        section.Entries.push_back(std::move(entry));
      }
    }
    out[it.key()] = std::move(section);
  }
}

void AppendPool(
    std::vector<ObjectFeatureRule> &out,
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

void RestoreLegacyFeatureMetadata(std::vector<ObjectFeatureRule> &rules,
                                  const std::vector<ObjectFeatureRule> &global)
{
  for (ObjectFeatureRule &rule : rules)
  {
    const auto authored = std::find_if(
        global.begin(), global.end(),
        [&rule](const ObjectFeatureRule &candidate)
        {
          if (rule.Mode == ObjectFeatureRuleMode::ScatterBlocks ||
              candidate.Mode == ObjectFeatureRuleMode::ScatterBlocks)
          {
            return rule.Mode == ObjectFeatureRuleMode::ScatterBlocks &&
                   candidate.Mode == ObjectFeatureRuleMode::ScatterBlocks &&
                   rule.Scatter.BlockName == candidate.Scatter.BlockName;
          }
          return rule.ObjectName == candidate.ObjectName;
        });
    if (authored == global.end())
    {
      continue;
    }

    // worldgen_sets v1 saved a feature's name and placement frequency but
    // discarded its strategy, ecology, surface rule, and species profile.
    // Recover only those authored details for profile-2 worlds; preserve all
    // per-world weights, biome selections, spacing, and seed offsets.
    if (rule.Mode == ObjectFeatureRuleMode::Object &&
        authored->Mode != ObjectFeatureRuleMode::Object)
    {
      rule.Mode = authored->Mode;
    }
    if (rule.Mode != authored->Mode)
    {
      continue;
    }
    rule.Tree = authored->Tree;
    rule.Shrub = authored->Shrub;
    rule.Ecology = authored->Ecology;
    rule.Surface = authored->Surface;
  }
}

void UpgradeLegacyFeatureMetadata(
    std::unordered_map<std::string, WorldGenObjectSection> &sections,
    const std::vector<ObjectFeatureRule> &global)
{
  for (auto &sectionPair : sections)
  {
    for (WorldGenObjectEntry &entry : sectionPair.second.Entries)
    {
      const auto authored = std::find_if(
          global.begin(), global.end(),
          [&entry](const ObjectFeatureRule &candidate)
          { return candidate.ObjectName == entry.ObjectName; });
      if (authored == global.end())
      {
        continue;
      }
      if (entry.Mode == ObjectFeatureRuleMode::Object &&
          authored->Mode != ObjectFeatureRuleMode::Object)
      {
        entry.Mode = authored->Mode;
      }
      if (entry.Mode != authored->Mode)
      {
        continue;
      }
      entry.Tree = authored->Tree;
      entry.Shrub = authored->Shrub;
      entry.Ecology = authored->Ecology;
      entry.Surface = authored->Surface;
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
  if (out.SchemaVersion < 1 || out.SchemaVersion > 2)
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
  const int schemaVersion = sets.SchemaVersion;
  auto writePool =
      [schemaVersion](const std::unordered_map<std::string,
                                               WorldGenObjectSection> &sections)
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
        else if (schemaVersion >= 2 &&
                 entry.Mode == ObjectFeatureRuleMode::ProceduralTree)
        {
          item["mode"] = "procedural_tree";
          item["species"] = entry.ObjectName;
          item["tree_profile"] = WriteTreeProfile(entry.Tree);
        }
        else if (schemaVersion >= 2 &&
                 entry.Mode == ObjectFeatureRuleMode::ProceduralShrub)
        {
          item["mode"] = "procedural_shrub";
          item["species"] = entry.ObjectName;
          item["shrub_profile"] = WriteShrubProfile(entry.Shrub);
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
        if (schemaVersion >= 2)
        {
          if (entry.Surface.Kind != SurfaceConstraintKind::AnyLand)
          {
            item["surface_constraint"] =
                SurfaceConstraintKindToString(entry.Surface.Kind);
          }
          if (entry.Surface.Kind == SurfaceConstraintKind::NearWater ||
              entry.Surface.Kind == SurfaceConstraintKind::WaterSurface)
          {
            item["near_water_radius"] = entry.Surface.NearWaterRadius;
          }
          WriteEcology(entry.Ecology, item);
        }
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
  sets.SchemaVersion = 2;
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
                                          const ObjectFeatureConfig &global,
                                          int worldgenProfileVersion)
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
  if (sets.SchemaVersion == 1 && worldgenProfileVersion >= 2)
  {
    RestoreLegacyFeatureMetadata(resolved.Vegetation, global.Vegetation);
    RestoreLegacyFeatureMetadata(resolved.GroundCover, global.GroundCover);
    RestoreLegacyFeatureMetadata(resolved.Decoration, global.Decoration);
    RestoreLegacyFeatureMetadata(resolved.Structures, global.Structures);
  }
  return resolved;
}

void UpgradeLegacyWorldGenSetMetadata(WorldGenSets &sets,
                                      const ObjectFeatureConfig &global,
                                      int worldgenProfileVersion)
{
  if (sets.SchemaVersion != 1 || worldgenProfileVersion < 2)
  {
    return;
  }
  UpgradeLegacyFeatureMetadata(sets.VegetationSections, global.Vegetation);
  UpgradeLegacyFeatureMetadata(sets.GroundCoverSections, global.GroundCover);
  UpgradeLegacyFeatureMetadata(sets.DecorationSections, global.Decoration);
  UpgradeLegacyFeatureMetadata(sets.StructureSections, global.Structures);
  sets.SchemaVersion = 2;
}

bool ValidateWorldGenSets(const WorldGenSets &sets, std::string &error)
{
  if (sets.SchemaVersion < 1 || sets.SchemaVersion > 2)
  {
    error = "worldgen_sets schema_version must be 1 or 2";
    return false;
  }
  (void)sets;
  return true;
}

} // namespace cutum
