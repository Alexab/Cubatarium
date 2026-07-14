#include "Creatures/Visual/CreatureRigidModelLoader.h"

#include <fstream>
#include <glm/glm.hpp>
#include <iostream>
#include <nlohmann/json.hpp>
#include <unordered_map>

namespace cutum
{

namespace
{

glm::vec3 ReadVec3(const nlohmann::json &arr, const glm::vec3 &fallback)
{
  if (!arr.is_array() || arr.size() < 3)
  {
    return fallback;
  }
  return glm::vec3(arr[0].get<float>(), arr[1].get<float>(),
                   arr[2].get<float>());
}

} // namespace

CreatureRigidModelCache &CreatureRigidModelCache::Instance()
{
  static CreatureRigidModelCache cache;
  return cache;
}

bool CreatureRigidModelCache::LoadParts(
    const std::string &speciesDir, const std::string &relativePath,
    std::vector<CreatureVisualPartDef> &outParts)
{
  static std::unordered_map<std::string, std::vector<CreatureVisualPartDef>>
      cache;

  const std::string cacheKey = speciesDir + "|" + relativePath;
  if (const auto it = cache.find(cacheKey); it != cache.end())
  {
    outParts = it->second;
    return !outParts.empty();
  }

  const std::string path = speciesDir + "/" + relativePath;
  std::ifstream file(path);
  if (!file)
  {
    std::cerr << "CreatureRigidModelCache: cannot open " << path << std::endl;
    return false;
  }

  nlohmann::json data;
  try
  {
    file >> data;
  }
  catch (const std::exception &ex)
  {
    std::cerr << "CreatureRigidModelCache: parse error in " << path << ": "
              << ex.what() << std::endl;
    return false;
  }

  if (!data.contains("parts") || !data["parts"].is_array())
  {
    std::cerr << "CreatureRigidModelCache: missing parts[] in " << path
              << std::endl;
    return false;
  }

  std::vector<CreatureVisualPartDef> parts;
  for (const auto &partJson : data["parts"])
  {
    CreatureVisualPartDef part;
    part.Id = partJson.value("id", "");
    part.textureStem = partJson.value("texture", "body");
    part.offsetBlocks = ReadVec3(
        partJson.value("offset", nlohmann::json::array()), part.offsetBlocks);
    part.sizeBlocks = ReadVec3(partJson.value("size", nlohmann::json::array()),
                               part.sizeBlocks);
    part.HasPivot = partJson.contains("pivot");
    if (part.HasPivot)
    {
      part.PivotBlocks = ReadVec3(
          partJson.value("pivot", nlohmann::json::array()), part.PivotBlocks);
    }
    part.LimbKind = partJson.value("limb", "");
    part.LimbAxis = partJson.value("limb_axis", "x");
    parts.push_back(part);
  }

  cache[cacheKey] = parts;
  outParts = parts;
  return !outParts.empty();
}

} // namespace cutum
