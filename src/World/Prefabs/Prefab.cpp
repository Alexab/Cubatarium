#include "World/Prefabs/Prefab.h"
#include "Blocks/BlockRegistry.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace cutum
{

void UPrefabLibrary::Load(const std::string &prefabs_folder,
                          UBlockRegistry &registry)
{
  Prefabs.clear();
  if (!std::filesystem::exists(prefabs_folder))
  {
    std::cerr << "UPrefabLibrary: folder not found: " << prefabs_folder
              << std::endl;
    return;
  }

  for (const auto &entry : std::filesystem::directory_iterator(prefabs_folder))
  {
    if (!entry.is_regular_file())
    {
      continue;
    }
    if (entry.path().extension() != ".json")
    {
      continue;
    }
    LoadFile(entry.path().string(), registry);
  }

  const auto userFolder = std::filesystem::path(prefabs_folder) / "user";
  if (std::filesystem::exists(userFolder))
  {
    for (const auto &entry : std::filesystem::directory_iterator(userFolder))
    {
      if (entry.is_regular_file() && entry.path().extension() == ".json")
      {
        LoadFile(entry.path().string(), registry);
      }
    }
  }

  std::cout << "UPrefabLibrary: loaded " << Prefabs.size() << " prefabs"
            << std::endl;
}

bool UPrefabLibrary::LoadFile(const std::string &path, UBlockRegistry &registry)
{
  std::ifstream file(path);
  if (!file.is_open())
  {
    return false;
  }

  try
  {
    json data = json::parse(file);
    Prefab prefab;
    prefab.Name =
        data.value("name", std::filesystem::path(path).stem().string());

    if (data.contains("anchor") && data["anchor"].is_array() &&
        data["anchor"].size() == 3)
    {
      prefab.anchor =
          glm::ivec3(data["anchor"][0].get<int>(), data["anchor"][1].get<int>(),
                     data["anchor"][2].get<int>());
    }

    prefab.boundsMin = glm::ivec3(std::numeric_limits<int>::max());
    prefab.boundsMax = glm::ivec3(std::numeric_limits<int>::min());

    for (const auto &blockEntry : data.at("blocks"))
    {
      const int dx = blockEntry.at("dx").get<int>();
      const int dy = blockEntry.at("dy").get<int>();
      const int dz = blockEntry.at("dz").get<int>();
      const std::string type = blockEntry.at("type").get<std::string>();
      const BlockId Id = registry.GetIdByTypeName(type);
      if (Id == BLOCK_AIR)
      {
        std::cerr << "UPrefabLibrary: unknown type '" << type << "' in " << path
                  << std::endl;
        continue;
      }
      PrefabVoxel voxel;
      voxel.offset = glm::ivec3(dx, dy, dz);
      voxel.Id = Id;
      prefab.voxels.push_back(voxel);

      const glm::ivec3 worldOffset = prefab.anchor + voxel.offset;
      prefab.boundsMin = glm::min(prefab.boundsMin, worldOffset);
      prefab.boundsMax = glm::max(prefab.boundsMax, worldOffset);
    }

    if (prefab.voxels.empty())
    {
      std::cerr << "UPrefabLibrary: empty prefab skipped: " << path
                << std::endl;
      return false;
    }

    Prefabs[prefab.Name] = std::move(prefab);
    return true;
  }
  catch (const json::exception &e)
  {
    std::cerr << "UPrefabLibrary: JSON error in " << path << ": " << e.what()
              << std::endl;
    return false;
  }
}

const Prefab *UPrefabLibrary::Get(const std::string &Name) const
{
  const auto it = Prefabs.find(Name);
  if (it == Prefabs.end())
  {
    return nullptr;
  }
  return &it->second;
}

std::vector<std::string> UPrefabLibrary::ListNames() const
{
  std::vector<std::string> names;
  names.reserve(Prefabs.size());
  for (const auto &entry : Prefabs)
  {
    names.push_back(entry.first);
  }
  return names;
}

} // namespace cutum
