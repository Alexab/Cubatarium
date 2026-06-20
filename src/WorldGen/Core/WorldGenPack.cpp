#include "WorldGen/Core/WorldGenPack.h"
#include "ThirdParty/stb_image.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace cutum
{

WorldGenPack UWorldGenPack::ActivePack;
std::string UWorldGenPack::ActivePackDir;

namespace
{

std::vector<uint8_t> gBiomeMapPixels;
int gBiomeMapW = 0;
int gBiomeMapH = 0;

BiomeId BiomeFromMapColor(uint8_t r, uint8_t g, uint8_t b)
{
  if (r == 0x2d && g == 0x8a && b == 0x3f)
  {
    return BiomeId::Forest;
  }
  if (r == 0xe8 && g == 0xd4 && b == 0x6a)
  {
    return BiomeId::Desert;
  }
  if (r == 0x7a && g == 0x7a && b == 0x7a)
  {
    return BiomeId::Hills;
  }
  if (r == 0xd8 && g == 0xe8 && b == 0xf0)
  {
    return BiomeId::Tundra;
  }
  return BiomeId::Plains;
}

bool LoadBiomeMapImage(const std::string &path)
{
  gBiomeMapPixels.clear();
  gBiomeMapW = 0;
  gBiomeMapH = 0;
  int comp = 0;
  unsigned char *data =
      stbi_load(path.c_str(), &gBiomeMapW, &gBiomeMapH, &comp, 3);
  if (!data || gBiomeMapW <= 0 || gBiomeMapH <= 0)
  {
    if (data)
    {
      stbi_image_free(data);
    }
    return false;
  }
  gBiomeMapPixels.assign(data, data + static_cast<size_t>(gBiomeMapW) *
                                       static_cast<size_t>(gBiomeMapH) * 3);
  stbi_image_free(data);
  return true;
}

void ParseBiomeHeight(const nlohmann::json &height, BiomeHeightProfile &out)
{
  if (!height.is_object())
  {
    return;
  }
  if (height.contains("base_offset"))
  {
    out.baseOffsetBlocks = height["base_offset"].get<float>();
  }
  if (height.contains("amplitude_mul"))
  {
    out.amplitudeMultiplier = height["amplitude_mul"].get<float>();
  }
  if (height.contains("volatility_mul"))
  {
    out.volatilityMultiplier = height["volatility_mul"].get<float>();
  }
}

void ParseBiomeFeatures(const nlohmann::json &features,
                        BiomePackDefinition &biomeDef)
{
  if (!features.is_object())
  {
    return;
  }
  for (const auto &[poolName, poolJson] : features.items())
  {
    (void)poolName;
    if (!poolJson.is_object())
    {
      continue;
    }
    for (const auto &[prefabName, weightJson] : poolJson.items())
    {
      if (!weightJson.is_number())
      {
        continue;
      }
      biomeDef.FeatureWeights[prefabName] = weightJson.get<float>();
    }
  }
}

void ParseSubBiomes(const nlohmann::json &subBiomes, BiomePackDefinition &biomeDef)
{
  if (!subBiomes.is_object())
  {
    return;
  }
  for (const auto &[subId, subJson] : subBiomes.items())
  {
    if (!subJson.is_object())
    {
      continue;
    }
    BiomeSubBiomePackRule rule;
    if (subJson.contains("subsurface") && subJson["subsurface"].is_string())
    {
      rule.SubsurfaceSlot = subJson["subsurface"].get<std::string>();
    }
    if (subJson.contains("vegetation_weight_mul"))
    {
      rule.VegetationWeightMul = subJson["vegetation_weight_mul"].get<float>();
    }
    if (subJson.contains("decoration_weight_mul"))
    {
      rule.DecorationWeightMul = subJson["decoration_weight_mul"].get<float>();
    }
    biomeDef.SubBiomes[subId] = rule;
  }
}

void ParsePalette(const nlohmann::json &palette, BiomePackDefinition &biomeDef)
{
  if (!palette.is_object())
  {
    return;
  }
  if (palette.contains("surface") && palette["surface"].is_string())
  {
    biomeDef.SurfaceSlot = palette["surface"].get<std::string>();
  }
  if (palette.contains("subsurface") && palette["subsurface"].is_string())
  {
    biomeDef.SubsurfaceSlot = palette["subsurface"].get<std::string>();
  }
}

bool ParsePipelineStage(const std::string &stage, WorldGenPackPipeline &pipeline)
{
  if (stage == "terrain")
  {
    return true;
  }
  if (stage == "fluids")
  {
    pipeline.Fluids = true;
    return true;
  }
  if (stage == "ores")
  {
    pipeline.Ores = true;
    return true;
  }
  if (stage == "caves")
  {
    pipeline.Caves = true;
    return true;
  }
  if (stage == "vegetation")
  {
    pipeline.Vegetation = true;
    return true;
  }
  if (stage == "decoration")
  {
    pipeline.Decoration = true;
    return true;
  }
  if (stage == "structures")
  {
    pipeline.Structures = true;
    return true;
  }
  if (stage == "lava_pools")
  {
    pipeline.LavaPools = true;
    return true;
  }
  if (stage == "fire_patch")
  {
    pipeline.FirePatch = true;
    return true;
  }
  return false;
}

void LoadPipelineJson(const std::filesystem::path &root, WorldGenPack &pack)
{
  const std::filesystem::path pipelineJson = root / "pipeline.json";
  if (!std::filesystem::exists(pipelineJson))
  {
    return;
  }
  try
  {
    std::ifstream file(pipelineJson);
    const nlohmann::json json = nlohmann::json::parse(file);
    if (!json.contains("stages") || !json["stages"].is_array())
    {
      return;
    }
    pack.Pipeline = WorldGenPackPipeline{};
    pack.Pipeline.Loaded = true;
    for (const auto &stage : json["stages"])
    {
      if (stage.is_string())
      {
        ParsePipelineStage(stage.get<std::string>(), pack.Pipeline);
      }
    }
  }
  catch (const std::exception &e)
  {
    std::cerr << "WorldGenPack: pipeline.json parse error: " << e.what()
              << std::endl;
  }
}

BiomePackDefinition ParseBiomeJson(const nlohmann::json &biomeJson,
                                     const std::string &biomeId)
{
  BiomePackDefinition biomeDef;
  if (biomeJson.contains("height"))
  {
    ParseBiomeHeight(biomeJson["height"], biomeDef.Height);
  }
  if (biomeJson.contains("palette"))
  {
    ParsePalette(biomeJson["palette"], biomeDef);
  }
  if (biomeJson.contains("features"))
  {
    ParseBiomeFeatures(biomeJson["features"], biomeDef);
  }
  if (biomeJson.contains("sub_biomes"))
  {
    ParseSubBiomes(biomeJson["sub_biomes"], biomeDef);
  }
  (void)biomeId;
  return biomeDef;
}

} // namespace

bool UWorldGenPack::LoadFromDirectory(const std::string &packDir)
{
  ActivePack = WorldGenPack{};
  ActivePackDir = packDir;
  const std::filesystem::path root(packDir);
  const std::filesystem::path packJson = root / "pack.json";
  if (!std::filesystem::exists(packJson))
  {
    std::cerr << "WorldGenPack: missing " << packJson.string() << std::endl;
    return false;
  }

  try
  {
    std::ifstream file(packJson);
    const nlohmann::json rootJson = nlohmann::json::parse(file);
    ActivePack.Id = rootJson.value("id", "default");
    const std::string mode = rootJson.value("biome_mode", "procedural");
    ActivePack.BiomeMode =
        mode == "image" ? WorldGenBiomeMode::Image : WorldGenBiomeMode::Procedural;
    ActivePack.BiomeMapBlockScale =
        std::max(1, rootJson.value("biome_map_block_scale", 4));
    if (rootJson.contains("biome_blend_radius"))
    {
      ActivePack.BiomeBlendRadius = rootJson["biome_blend_radius"].get<float>();
    }
    if (rootJson.contains("biome_map_image") &&
        rootJson["biome_map_image"].is_string())
    {
      ActivePack.BiomeMapImagePath = rootJson["biome_map_image"].get<std::string>();
    }
    if (rootJson.contains("pipeline") && rootJson["pipeline"].is_array())
    {
      ActivePack.Pipeline = WorldGenPackPipeline{};
      ActivePack.Pipeline.Loaded = true;
      for (const auto &stage : rootJson["pipeline"])
      {
        if (stage.is_string())
        {
          ParsePipelineStage(stage.get<std::string>(), ActivePack.Pipeline);
        }
      }
    }
  }
  catch (const std::exception &e)
  {
    std::cerr << "WorldGenPack: parse error: " << e.what() << std::endl;
    return false;
  }

  LoadPipelineJson(root, ActivePack);

  const std::filesystem::path biomesDir = root / "biomes";
  if (std::filesystem::exists(biomesDir))
  {
    for (const auto &entry : std::filesystem::directory_iterator(biomesDir))
    {
      if (!entry.is_regular_file() || entry.path().extension() != ".json")
      {
        continue;
      }
      try
      {
        std::ifstream biomeFile(entry.path());
        const nlohmann::json biomeJson = nlohmann::json::parse(biomeFile);
        const std::string biomeId =
            biomeJson.value("id", entry.path().stem().string());
        ActivePack.Biomes[biomeId] = ParseBiomeJson(biomeJson, biomeId);
      }
      catch (const std::exception &e)
      {
        std::cerr << "WorldGenPack: biome parse error in "
                  << entry.path().string() << ": " << e.what() << std::endl;
      }
    }
  }

  if (ActivePack.BiomeMode == WorldGenBiomeMode::Image &&
      !ActivePack.BiomeMapImagePath.empty())
  {
    const std::filesystem::path imagePath = root / ActivePack.BiomeMapImagePath;
    if (!LoadBiomeMapImage(imagePath.string()))
    {
      std::cerr << "WorldGenPack: failed to load biome map image "
                << imagePath.string() << std::endl;
      ActivePack.BiomeMode = WorldGenBiomeMode::Procedural;
    }
  }

  std::cout << "WorldGenPack: loaded '" << ActivePack.Id << "' with "
            << ActivePack.Biomes.size() << " biome profile(s)"
            << (ActivePack.Pipeline.Loaded ? " + pipeline" : "") << std::endl;
  return true;
}

bool UWorldGenPack::LoadPackId(const std::string &packId)
{
  const std::string id = packId.empty() ? "default" : packId;
  const std::string dir = "content/worldgen_packs/" + id;
  if (ActivePackDir == dir && ActivePack.Id == id && !ActivePack.Biomes.empty())
  {
    return true;
  }
  return LoadFromDirectory(dir);
}

bool UWorldGenPack::ReloadActive()
{
  if (ActivePackDir.empty())
  {
    return LoadPackId("default");
  }
  return LoadFromDirectory(ActivePackDir);
}

std::vector<std::string> UWorldGenPack::ListPackIds()
{
  std::vector<std::string> ids;
  const std::filesystem::path root("content/worldgen_packs");
  if (!std::filesystem::exists(root))
  {
    return ids;
  }
  for (const auto &entry : std::filesystem::directory_iterator(root))
  {
    if (!entry.is_directory())
    {
      continue;
    }
    if (std::filesystem::exists(entry.path() / "pack.json"))
    {
      ids.push_back(entry.path().filename().string());
    }
  }
  std::sort(ids.begin(), ids.end());
  return ids;
}

const WorldGenPack &UWorldGenPack::Get() { return ActivePack; }

const BiomeHeightProfile *UWorldGenPack::HeightProfileFor(
    const std::string &biomeId)
{
  const BiomePackDefinition *def = BiomeDefinitionFor(biomeId);
  if (!def)
  {
    return nullptr;
  }
  return &def->Height;
}

const BiomePackDefinition *UWorldGenPack::BiomeDefinitionFor(
    const std::string &biomeId)
{
  const auto it = ActivePack.Biomes.find(biomeId);
  if (it == ActivePack.Biomes.end())
  {
    return nullptr;
  }
  return &it->second;
}

float UWorldGenPack::FeatureWeightMultiplier(const std::string &biomeId,
                                             const std::string &prefabName)
{
  const BiomePackDefinition *def = BiomeDefinitionFor(biomeId);
  if (!def)
  {
    return 1.0f;
  }
  const auto prefabIt = def->FeatureWeights.find(prefabName);
  if (prefabIt == def->FeatureWeights.end())
  {
    return 1.0f;
  }
  return std::max(0.1f, prefabIt->second);
}

float UWorldGenPack::SubBiomePoolWeightMultiplier(const std::string &biomeId,
                                                  SubBiomeId subBiome,
                                                  PrefabFeaturePool pool)
{
  const BiomePackDefinition *def = BiomeDefinitionFor(biomeId);
  if (!def)
  {
    return 1.0f;
  }
  const auto subIt = def->SubBiomes.find(SubBiomeIdToString(subBiome));
  if (subIt == def->SubBiomes.end())
  {
    return 1.0f;
  }
  if (pool == PrefabFeaturePool::Vegetation)
  {
    return std::max(0.1f, subIt->second.VegetationWeightMul);
  }
  if (pool == PrefabFeaturePool::Decoration)
  {
    return std::max(0.1f, subIt->second.DecorationWeightMul);
  }
  return 1.0f;
}

BiomeId UWorldGenPack::BiomeAtImage(int worldX, int worldZ)
{
  if (gBiomeMapPixels.empty() || gBiomeMapW <= 0 || gBiomeMapH <= 0)
  {
    return BiomeId::Plains;
  }
  const int scale = std::max(1, ActivePack.BiomeMapBlockScale);
  const int px = ((worldX / scale) % gBiomeMapW + gBiomeMapW) % gBiomeMapW;
  const int pz = ((worldZ / scale) % gBiomeMapH + gBiomeMapH) % gBiomeMapH;
  const size_t idx =
      (static_cast<size_t>(pz) * static_cast<size_t>(gBiomeMapW) +
       static_cast<size_t>(px)) *
      3;
  if (idx + 2 >= gBiomeMapPixels.size())
  {
    return BiomeId::Plains;
  }
  return BiomeFromMapColor(gBiomeMapPixels[idx], gBiomeMapPixels[idx + 1],
                           gBiomeMapPixels[idx + 2]);
}

} // namespace cutum
