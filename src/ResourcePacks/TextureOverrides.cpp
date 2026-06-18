#include "ResourcePacks/TextureOverrides.h"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace cutum
{

namespace
{

int FaceIndexFromName(const std::string &face)
{
  if (face == "top")
  {
    return 0;
  }
  if (face == "bottom")
  {
    return 1;
  }
  if (face == "sides" || face == "side")
  {
    return 2;
  }
  if (face == "north")
  {
    return 2;
  }
  if (face == "south")
  {
    return 3;
  }
  if (face == "west")
  {
    return 4;
  }
  if (face == "east")
  {
    return 5;
  }
  return -1;
}

void ParseOverrideEntry(const nlohmann::json &entry,
                        std::vector<TextureFaceOverride> &out)
{
  if (!entry.is_object())
  {
    return;
  }
  TextureFaceOverride ov;
  if (entry.contains("stem") && entry["stem"].is_string())
  {
    ov.Stem = entry["stem"].get<std::string>();
  }
  if (entry.contains("faces") && entry["faces"].is_array())
  {
    for (const auto &f : entry["faces"])
    {
      if (!f.is_string())
      {
        continue;
      }
      const int idx = FaceIndexFromName(f.get<std::string>());
      if (idx >= 0)
      {
        ov.Faces.push_back(idx);
      }
    }
  }
  if (ov.Faces.empty())
  {
    ov.Faces = {0, 1, 2, 3, 4, 5};
  }
  if (!ov.Stem.empty())
  {
    out.push_back(std::move(ov));
  }
}

TextureOverrideMap LoadFromJson(const std::filesystem::path &path)
{
  TextureOverrideMap result;
  std::ifstream in(path);
  if (!in.is_open())
  {
    return result;
  }
  try
  {
    const nlohmann::json root = nlohmann::json::parse(in);
    if (!root.is_object())
    {
      return result;
    }
    for (const auto &[blockName, blockVal] : root.items())
    {
      std::vector<TextureFaceOverride> overrides;
      if (blockVal.is_array())
      {
        for (const auto &entry : blockVal)
        {
          ParseOverrideEntry(entry, overrides);
        }
      }
      else if (blockVal.is_object())
      {
        if (blockVal.contains("overrides") && blockVal["overrides"].is_array())
        {
          for (const auto &entry : blockVal["overrides"])
          {
            ParseOverrideEntry(entry, overrides);
          }
        }
        else
        {
          ParseOverrideEntry(blockVal, overrides);
        }
      }
      if (!overrides.empty())
      {
        result[blockName] = std::move(overrides);
      }
    }
  }
  catch (const nlohmann::json::exception &e)
  {
    std::cerr << "TextureOverrides: parse error " << path << ": " << e.what()
              << std::endl;
  }
  return result;
}

} // namespace

TextureOverrideMap LoadTextureOverrides(const std::filesystem::path &packRoot)
{
  const auto jsonPath = packRoot / "texture_overrides.json";
  if (std::filesystem::exists(jsonPath))
  {
    return LoadFromJson(jsonPath);
  }
  const auto yamlPath = packRoot / "texture_overrides.yaml";
  if (std::filesystem::exists(yamlPath))
  {
    std::cerr << "TextureOverrides: found " << yamlPath
              << " but no texture_overrides.json — run "
                 "tools/sync_texture_overrides.py"
              << std::endl;
  }
  return {};
}

} // namespace cutum
