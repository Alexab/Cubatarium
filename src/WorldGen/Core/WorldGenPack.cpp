#include "WorldGen/Core/WorldGenPack.h"
#include "ThirdParty/stb_image.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace cutum
{

WorldGenPack UWorldGenPack::ActivePack;

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

} // namespace

bool UWorldGenPack::LoadFromDirectory(const std::string &packDir)
{
  ActivePack = WorldGenPack{};
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
    if (rootJson.contains("biome_map_image") &&
        rootJson["biome_map_image"].is_string())
    {
      ActivePack.BiomeMapImagePath = rootJson["biome_map_image"].get<std::string>();
    }
  }
  catch (const std::exception &e)
  {
    std::cerr << "WorldGenPack: parse error: " << e.what() << std::endl;
    return false;
  }

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
        const std::string biomeId = biomeJson.value("id", entry.path().stem().string());
        BiomeHeightProfile profile;
        if (biomeJson.contains("height"))
        {
          ParseBiomeHeight(biomeJson["height"], profile);
        }
        ActivePack.BiomeHeightProfiles[biomeId] = profile;
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
            << ActivePack.BiomeHeightProfiles.size() << " biome profile(s)"
            << std::endl;
  return true;
}

const WorldGenPack &UWorldGenPack::Get() { return ActivePack; }

const BiomeHeightProfile *UWorldGenPack::HeightProfileFor(
    const std::string &biomeId)
{
  const auto it = ActivePack.BiomeHeightProfiles.find(biomeId);
  if (it == ActivePack.BiomeHeightProfiles.end())
  {
    return nullptr;
  }
  return &it->second;
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
