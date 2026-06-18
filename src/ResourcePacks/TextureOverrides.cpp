#include "ResourcePacks/TextureOverrides.h"
#include <cctype>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>

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

std::string Trim(const std::string &s)
{
  size_t start = 0;
  while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])))
  {
    ++start;
  }
  size_t end = s.size();
  while (end > start &&
         std::isspace(static_cast<unsigned char>(s[end - 1])))
  {
    --end;
  }
  return s.substr(start, end - start);
}

void CommitOverrideEntry(std::vector<TextureFaceOverride> &overrides,
                         TextureFaceOverride &entry)
{
  if (entry.Faces.empty())
  {
    entry.Faces = {0, 1, 2, 3, 4, 5};
  }
  if (!entry.Stem.empty())
  {
    overrides.push_back(entry);
  }
  entry = TextureFaceOverride{};
}

void ParseFacesBracketList(const std::string &value,
                           TextureFaceOverride &entry)
{
  std::string inner = value;
  const auto lb = inner.find('[');
  const auto rb = inner.find(']');
  if (lb != std::string::npos && rb != std::string::npos && rb > lb)
  {
    inner = inner.substr(lb + 1, rb - lb - 1);
  }
  std::stringstream ss(inner);
  std::string token;
  while (std::getline(ss, token, ','))
  {
    token = Trim(token);
    const int idx = FaceIndexFromName(token);
    if (idx >= 0)
    {
      entry.Faces.push_back(idx);
    }
  }
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
  CommitOverrideEntry(out, ov);
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

TextureOverrideMap LoadFromYaml(const std::filesystem::path &path)
{
  TextureOverrideMap result;
  std::ifstream in(path);
  if (!in.is_open())
  {
    return result;
  }
  std::string currentBlock;
  std::vector<TextureFaceOverride> currentOverrides;
  TextureFaceOverride currentEntry;
  std::string line;
  while (std::getline(in, line))
  {
    const std::string trimmed = Trim(line);
    if (trimmed.empty() || trimmed[0] == '#')
    {
      continue;
    }
    if (trimmed[0] != ' ' && trimmed[0] != '\t' && trimmed.back() == ':')
    {
      if (!currentBlock.empty())
      {
        CommitOverrideEntry(currentOverrides, currentEntry);
        if (!currentOverrides.empty())
        {
          result[currentBlock] = std::move(currentOverrides);
        }
        currentOverrides.clear();
      }
      currentBlock = trimmed.substr(0, trimmed.size() - 1);
      currentBlock = Trim(currentBlock);
      continue;
    }
    if (trimmed.rfind("- ", 0) == 0)
    {
      CommitOverrideEntry(currentOverrides, currentEntry);
      continue;
    }
    const auto colon = trimmed.find(':');
    if (colon == std::string::npos)
    {
      continue;
    }
    const std::string key = Trim(trimmed.substr(0, colon));
    const std::string value = Trim(trimmed.substr(colon + 1));
    if (key == "faces")
    {
      ParseFacesBracketList(value, currentEntry);
    }
    else if (key == "stem")
    {
      currentEntry.Stem = value;
    }
  }
  if (!currentBlock.empty())
  {
    CommitOverrideEntry(currentOverrides, currentEntry);
    if (!currentOverrides.empty())
    {
      result[currentBlock] = std::move(currentOverrides);
    }
  }
  return result;
}

} // namespace

TextureOverrideMap LoadTextureOverrides(const std::filesystem::path &packRoot)
{
  const auto jsonPath = packRoot / "texture_overrides.json";
  const auto yamlPath = packRoot / "texture_overrides.yaml";
  if (std::filesystem::exists(jsonPath))
  {
    return LoadFromJson(jsonPath);
  }
  if (std::filesystem::exists(yamlPath))
  {
    return LoadFromYaml(yamlPath);
  }
  return {};
}

} // namespace cutum
