#include "ResourcePacks/ResourcePack.h"
#include "ResourcePacks/TextureOverrides.h"
#include "Render/Textures/TextureBase.h"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace cutum
{

namespace fs = std::filesystem;
using json = nlohmann::json;

void StripUtf8Bom(std::string &text)
{
  if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
      static_cast<unsigned char>(text[1]) == 0xBB &&
      static_cast<unsigned char>(text[2]) == 0xBF)
  {
    text.erase(0, 3);
  }
}

std::string PackQualifiedTextureStem(const std::string &packId,
                                     const std::string &stem)
{
  return packId + "/" + stem;
}

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
  std::string raw = buffer.str();
  StripUtf8Bom(raw);
  try
  {
    const json d = json::parse(raw);
    ResourcePackManifest m;
    m.Root = root;
    m.Id = d.value("id", root.filename().string());
    m.Name = d.value("name", m.Id);
    m.License = d.value("license", "");
    m.Version = d.value("version", 1);
    m.Priority = d.value("priority", 0);
    m.Resolution = d.value("resolution", 16);
    m.PackFormat = d.value("pack_format", 1);
    m.MinGameVersion = d.value("min_game_version", "");
    m.AllowResolutionMix = d.value("allow_resolution_mix", false);
    if (d.contains("depends") && d["depends"].is_array())
    {
      for (const auto &dep : d["depends"])
      {
        if (dep.is_string())
        {
          m.Depends.push_back(dep.get<std::string>());
        }
      }
    }
    if (d.contains("conflicts") && d["conflicts"].is_array())
    {
      for (const auto &c : d["conflicts"])
      {
        if (c.is_string())
        {
          m.Conflicts.push_back(c.get<std::string>());
        }
      }
    }
    const std::string role = d.value("worldgen_role", "secondary");
    m.Role =
        (role == "primary") ? WorldgenRole::Primary : WorldgenRole::Secondary;
    const std::string merge = d.value("merge_mode", "skip_existing");
    if (merge == "override")
    {
      m.MergeMode = PackMergeMode::Override;
    }
    else if (merge == "duplicate")
    {
      m.MergeMode = PackMergeMode::Duplicate;
    }
    else
    {
      m.MergeMode = PackMergeMode::SkipExisting;
    }
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
    std::string raw = buffer.str();
    StripUtf8Bom(raw);
    try
    {
      const json d = json::parse(raw);
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

TextureOverrideMap
UResourcePack::LoadTextureOverrideMap(const ResourcePackManifest &manifest)
{
  return LoadTextureOverrides(manifest.Root);
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
      storage.Register(PackQualifiedTextureStem(manifest.Id, stem),
                       entry.path().string());
    }
  }
}

} // namespace cutum
