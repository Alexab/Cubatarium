#ifndef OBJECT_LIBRARY_H
#define OBJECT_LIBRARY_H

#include "ResourcePacks/ResourcePack.h"
#include "World/Math/BlockTypes.h"
#include <filesystem>
#include <glm/glm.hpp>
#include <memory>
#include <mutex>
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

// Builds one candidate; registry/input are borrowed. Unknown-type diagnostics
// keep the same caller-owned deduplication set used by LoadFile.
WorldObjectDefinition CompilePrefabCatalog(const nlohmann::json &data,
    UBlockRegistry &registry, const std::string &path,
    const std::string &registerName, ObjectOrigin origin,
    const std::string &sourcePackId,
    std::unordered_set<std::string> &loggedUnknownTypes);

class UObjectLibrary
{
public:
  void Load(const std::string &objects_folder, UBlockRegistry &registry);
  void LoadMerged(const std::filesystem::path &baseFolder,
                  const std::vector<ResourcePackManifest> &packs,
                  UBlockRegistry &registry);
  void RebindBlockIds(UBlockRegistry &registry);
  bool ValidateCriticalPrefabs() const;
  /// Thread-safe; returned immutable definition survives reload and rebind.
  std::shared_ptr<const WorldObjectDefinition>
  GetShared(const std::string &Name) const;
  /// Convenience for main-thread short reads. Holds a TLS shared_ptr keep-alive.
  const WorldObjectDefinition *Get(const std::string &Name) const;
  static std::shared_ptr<const WorldObjectDefinition> GetThreadReadSnapshot() {return ThreadReadKeep();}
  static void ReleaseThreadRead() noexcept {ThreadReadKeep().reset();}
  std::vector<std::string> ListNames() const;
  std::string GetDisplayName(const std::string &Name) const;
  std::vector<std::string> GetTags(const std::string &Name) const;
  ObjectOrigin GetOrigin(const std::string &Name) const;

  // Quiescent load/rebind diagnostic only; does not retain returned objects.
  template<class Inspector> void InspectStorage(Inspector &out) const {
    std::shared_lock lock(ObjectsMutex);
    out.Map(Objects);out.Map(LoggedUnknownTypes);
    for(const auto &entry:Objects) {out.Text(entry.first);out.Prefab(entry.second);}
    for(const auto &name:LoggedUnknownTypes) out.Text(name);
  }

private:
  static std::shared_ptr<const WorldObjectDefinition> &ThreadReadKeep() {
    thread_local std::shared_ptr<const WorldObjectDefinition> keep;return keep;
  }
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
  // Serialize complete writer operations while readers can keep old versions.
  mutable std::mutex MutationMutex;
  mutable std::shared_mutex ObjectsMutex;
};

} // namespace cutum

#endif
