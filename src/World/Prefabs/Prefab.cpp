#include "World/Prefabs/Prefab.h"
#include "Blocks/BlockRegistry.h"
#include "ResourcePacks/BlockNameUtil.h"
#include "ResourcePacks/ResourcePack.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace cutum
{

void UPrefabLibrary::LoadDirectory(const std::filesystem::path &folder,
                                  const std::string &namePrefix,
                                  UBlockRegistry &registry)
{
  if (!std::filesystem::exists(folder))
  {
    return;
  }
  for (const auto &entry : std::filesystem::directory_iterator(folder))
  {
    if (!entry.is_regular_file() || entry.path().extension() != ".json")
    {
      continue;
    }
    const std::string localName = entry.path().stem().string();
    const std::string registerName =
        namePrefix.empty() ? localName
                           : MakeQualifiedBlockName(namePrefix, localName);
    LoadFile(entry.path().string(), registry, registerName);
  }
}

void UPrefabLibrary::LoadMerged(
    const std::filesystem::path &baseFolder,
    const std::vector<ResourcePackManifest> &packs, UBlockRegistry &registry)
{
  Prefabs.clear();
  LoadDirectory(baseFolder, "", registry);
  LoadDirectory(baseFolder / "user", "", registry);
  for (const auto &pack : packs)
  {
    LoadDirectory(pack.Root / "prefabs", pack.Id, registry);
  }
  std::cout << "UPrefabLibrary: loaded " << Prefabs.size() << " prefabs"
            << std::endl;
}

void UPrefabLibrary::Load(const std::string &prefabs_folder,
                          UBlockRegistry &registry)
{
  LoadMerged(std::filesystem::path(prefabs_folder), {}, registry);
}

bool UPrefabLibrary::LoadFile(const std::string &path, UBlockRegistry &registry,
                              const std::string &registerName)
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
    prefab.Name = registerName.empty()
                      ? data.value("name",
                                   std::filesystem::path(path).stem().string())
                      : registerName;

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
