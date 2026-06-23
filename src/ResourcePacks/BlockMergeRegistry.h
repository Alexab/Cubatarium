#ifndef BLOCKMERGEREGISTRY_H
#define BLOCKMERGEREGISTRY_H

#include "Blocks/BlockDefinition.h"
#include "ResourcePacks/PlaceholderTextureCache.h"
#include "ResourcePacks/ResourcePack.h"
#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cutum
{

class UBlockDefinitionStorage;
class UTextureBaseStorage;

struct MergedCubeDesc
{
  std::string Name;
  BlockId Id{BLOCK_AIR};
  std::array<std::string, 6> Stems{};
  int AnimFrames{1};
};

class UBlockMergeRegistry
{
public:
  void Rebuild(const std::vector<ResourcePackManifest> &packs,
               std::shared_ptr<UPlaceholderTextureCache> placeholder,
               int placeholderTileSize);

  BlockId ResolveName(const std::string &name);
  BlockId ResolveBlockName(const std::string &name);

  void SetWorldgenOwnerPackId(const std::string &packId)
  {
    WorldgenOwnerPackId = packId;
  }
  const std::string &GetWorldgenOwnerPackId() const
  {
    return WorldgenOwnerPackId;
  }
  void SetPrimaryPackIds(const std::vector<std::string> &ids)
  {
    PrimaryPackIds = ids;
  }

  bool HasBlock(const std::string &name) const
  {
    return BlocksByName.find(name) != BlocksByName.end();
  }

  BlockId RegisterRuntimeBlock(const BlockDefinition &def,
                               const std::array<std::string, 6> &stems);
  void FlushRuntimeOverlay();
  void UnregisterRuntimeBlock(const std::string &name);

  void PopulateBlockDefinitionStorage(UBlockDefinitionStorage &out) const;
  void PopulateTextureBaseStorage(UTextureBaseStorage &out) const;

  const std::vector<MergedCubeDesc> &GetCubeDescriptors() const
  {
    return CubeDescs;
  }
  const std::unordered_map<std::string, BlockId> &GetNameToId() const
  {
    return NameToId;
  }
  const std::string *GetTypeNameById(BlockId id) const
  {
    const auto it = IdToName.find(id);
    if (it != IdToName.end())
    {
      return &it->second;
    }
    return nullptr;
  }

  std::string ComputeCatalogFingerprint() const;

private:
  struct MergedEntry
  {
    BlockDefinition Definition;
    std::array<std::string, 6> Stems{};
    std::string OwnerPackId;
    bool IsQualifiedDuplicate{false};
  };

  void MergeBlockCatalog(const std::vector<ResourcePackManifest> &sorted);
  void ResolveBlockDefinitions(const std::vector<ResourcePackManifest> &sorted);
  void ResolveTextureAtlas(const std::vector<ResourcePackManifest> &sorted);
  void ApplyTextureOverrides(const std::vector<ResourcePackManifest> &sorted);

  std::string ResolveStemInPack(const std::string &stem,
                                const ResourcePackManifest &pack) const;
  void AssignRuntimeIds();
  BlockId CreateSyntheticBlock(const std::string &name);
  static bool IsPackOwnedEntry(const MergedEntry &entry);

  std::vector<ResourcePackManifest> ActivePacks;
  std::vector<std::string> PrimaryPackIds;
  std::string WorldgenOwnerPackId;
  std::unordered_map<std::string, MergedEntry> BlocksByName;
  std::unordered_map<std::string, BlockId> NameToId;
  std::unordered_map<BlockId, std::string> IdToName;
  std::vector<MergedCubeDesc> CubeDescs;
  std::vector<std::pair<BlockDefinition, std::array<std::string, 6>>>
      RuntimeOverlay;
  bool RuntimeOverlayDirty{false};
  std::shared_ptr<UPlaceholderTextureCache> PlaceholderCache;
  int PlaceholderTileSize{16};
  static const std::unordered_set<std::string> kTierAWorldgenNames;
};

} // namespace cutum

#endif
