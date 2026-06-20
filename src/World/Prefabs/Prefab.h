#ifndef PREFAB_H
#define PREFAB_H

#include "ResourcePacks/ResourcePack.h"
#include "World/Math/BlockTypes.h"
#include <filesystem>
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace cutum
{

struct PrefabVoxel
{
  glm::ivec3 offset;
  BlockId Id{BLOCK_AIR};
};

struct Prefab
{
  std::string Name;
  std::string Category;
  std::string DisplayName;
  int PlacementYOffset{0};
  glm::ivec3 anchor{0};
  std::vector<PrefabVoxel> voxels;
  glm::ivec3 boundsMin{0};
  glm::ivec3 boundsMax{0};
};

class UBlockRegistry;

class UPrefabLibrary
{
public:
  void Load(const std::string &prefabs_folder, UBlockRegistry &registry);
  void LoadMerged(const std::filesystem::path &baseFolder,
                  const std::vector<ResourcePackManifest> &packs,
                  UBlockRegistry &registry);
  const Prefab *Get(const std::string &Name) const;
  std::vector<std::string> ListNames() const;
  std::string GetDisplayName(const std::string &Name) const;
  std::string GetCategory(const std::string &Name) const;

private:
  bool LoadFile(const std::string &path, UBlockRegistry &registry,
                const std::string &registerName = {});
  void LoadDirectory(const std::filesystem::path &folder,
                     const std::string &namePrefix,
                     UBlockRegistry &registry);
  void LoadDirectoryRecursive(const std::filesystem::path &folder,
                              const std::string &namePrefix,
                              UBlockRegistry &registry);

  std::unordered_map<std::string, Prefab> Prefabs;
};

} // namespace cutum

#endif
