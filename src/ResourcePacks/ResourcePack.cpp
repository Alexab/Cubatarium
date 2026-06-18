#include "ResourcePacks/ResourcePack.h"
#include "Render/Textures/TextureBase.h"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace cutum
{

namespace fs = std::filesystem;
using json = nlohmann::json;

std::optional<ResourcePackManifest>
UResourcePack::LoadManifest(const fs::path &root)
{
  const fs::path packJson = root / "pack.json";
  if (!fs::exists(packJson))
  {
    std::cerr << "UResourcePack: missing pack.json in " << root << std::endl;
    return std::nullopt;
  }
  std::ifstream in(packJson);
  if (!in.is_open())
  {
    return std::nullopt;
  }
  std::stringstream buffer;
  buffer << in.rdbuf();
  try
  {
    const json d = json::parse(buffer.str());
    ResourcePackManifest m;
    m.Root = root;
    m.Id = d.value("id", root.filename().string());
    m.Name = d.value("name", m.Id);
    m.License = d.value("license", "");
    m.Version = d.value("version", 1);
    m.Priority = d.value("priority", 0);
    m.Resolution = d.value("resolution", 16);
    return m;
  }
  catch (const json::exception &e)
  {
    std::cerr << "UResourcePack: pack.json parse error " << root << ": "
              << e.what() << std::endl;
    return std::nullopt;
  }
}

std::vector<ResourcePackBlock>
UResourcePack::LoadBlocks(const ResourcePackManifest &manifest)
{
  std::vector<ResourcePackBlock> blocks;
  const fs::path blocksDir = manifest.Root / "blocks";
  if (!fs::exists(blocksDir))
  {
    return blocks;
  }
  for (const auto &entry : fs::directory_iterator(blocksDir))
  {
    if (entry.path().extension() != ".json")
    {
      continue;
    }
    std::ifstream file(entry.path());
    if (!file.is_open())
    {
      continue;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    try
    {
      const json d = json::parse(buffer.str());
      const ParsedBlockJson parsed = ParseBlockFromJson(d, true);
      if (!parsed.Valid)
      {
        std::cerr << "UResourcePack: skip invalid block " << entry.path()
                  << std::endl;
        continue;
      }
      ResourcePackBlock block;
      block.Definition = parsed.Definition;
      block.TextureStems = parsed.TextureStems;
      blocks.push_back(std::move(block));
    }
    catch (const json::exception &e)
    {
      std::cerr << "UResourcePack: parse error " << entry.path() << ": "
                << e.what() << std::endl;
    }
  }
  return blocks;
}

fs::path UResourcePack::TexturePath(const ResourcePackManifest &manifest,
                                    const std::string &stem)
{
  return manifest.Root / "textures" / "blocks" / (stem + ".png");
}

void UResourcePack::RegisterTextures(const ResourcePackManifest &manifest,
                                     UTextureBaseStorage &storage)
{
  const fs::path texDir = manifest.Root / "textures" / "blocks";
  if (!fs::exists(texDir))
  {
    return;
  }
  for (const auto &entry : fs::directory_iterator(texDir))
  {
    if (entry.path().extension() == ".png")
    {
      const std::string stem = entry.path().stem().string();
      storage.Register(stem, entry.path().string());
    }
  }
}

} // namespace cutum
