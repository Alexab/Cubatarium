#ifndef OBJECT_LIBRARY_H
#define OBJECT_LIBRARY_H

#include "ResourcePacks/ResourcePack.h"
#include "World/Math/BlockTypes.h"
#include <filesystem>
#include <glm/glm.hpp>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cutum
{

enum class ObjectOrigin
{
  Builtin,
  User,
  Imported,
  ResourcePack,
};

enum class ObjectPlacementMode
{
  Default,
  SurfaceLayer,
  VerticalPlant,
};

struct ObjectVoxel
{
  glm::ivec3 offset;
  BlockId Id{BLOCK_AIR};
  std::string Type;
};

struct WorldObjectDefinition
{
  std::string Name;
  std::vector<std::string> Tags;
  std::string DisplayName;
  ObjectOrigin Origin{ObjectOrigin::Builtin};
  std::string SourcePackId;
  int PlacementYOffset{0};
  ObjectPlacementMode PlacementMode{ObjectPlacementMode::Default};
  glm::ivec3 anchor{0};
  std::vector<ObjectVoxel> voxels;
  glm::ivec3 boundsMin{0};
  glm::ivec3 boundsMax{0};
  bool Hidden{false};
};

class UBlockRegistry;

class UObjectLibrary
{
public:
  void Load(const std::string &objects_folder, UBlockRegistry &registry);
  void LoadMerged(const std::filesystem::path &baseFolder,
                  const std::vector<ResourcePackManifest> &packs,
                  UBlockRegistry &registry);
  void RebindBlockIds(UBlockRegistry &registry);
  bool ValidateCriticalPrefabs() const;
  /// Thread-safe; returned shared_ptr keeps the definition alive across reloads.
  std::shared_ptr<const WorldObjectDefinition>
  GetShared(const std::string &Name) const;
  /// Convenience for main-thread short reads. Holds a TLS shared_ptr keep-alive.
  const WorldObjectDefinition *Get(const std::string &Name) const;
  std::vector<std::string> ListNames() const;
  std::string GetDisplayName(const std::string &Name) const;
  std::vector<std::string> GetTags(const std::string &Name) const;
  ObjectOrigin GetOrigin(const std::string &Name) const;

private:
  bool LoadFile(const std::string &path, UBlockRegistry &registry,
                const std::string &registerName, ObjectOrigin origin,
                const std::string &sourcePackId = {});
  void LoadDirectory(const std::filesystem::path &folder,
                     const std::string &namePrefix, ObjectOrigin origin,
                     UBlockRegistry &registry);
  void LoadDirectoryRecursive(const std::filesystem::path &folder,
                              const std::string &namePrefix, ObjectOrigin origin,
                              UBlockRegistry &registry);

  std::unordered_map<std::string, std::shared_ptr<WorldObjectDefinition>>
      Objects;
  std::unordered_set<std::string> LoggedUnknownTypes;
  mutable std::shared_mutex ObjectsMutex;
};

} // namespace cutum

#endif
