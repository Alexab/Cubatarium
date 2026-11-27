#ifndef BLOCKMERGEREGISTRY_H
#define BLOCKMERGEREGISTRY_H

#include "Blocks/BlockDefinition.h"
#include "ResourcePacks/PlaceholderTextureCache.h"
#include "ResourcePacks/ResourcePack.h"
#include <array>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
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

struct RuntimeOverlayFlushResult
{
  std::vector<MergedCubeDesc> PatchedDescriptors;
  std::vector<BlockId> RemovedBlockIds;
};

struct BlockMergeSourceLayout
{
  size_t pending{},block{},merged_node{},name_node{},id_node{},memo_node{},owner_node{},descriptor{};
  bool supported{};
};

class UBlockMergeRegistry
{
public:
  static BlockMergeSourceLayout SourceLayout() noexcept;
  void Rebuild(const std::vector<ResourcePackManifest> &packs,
               std::shared_ptr<UPlaceholderTextureCache> placeholder,
               int placeholderTileSize);

  BlockId ResolveName(const std::string &name);
  BlockId ResolveBlockName(const std::string &name);
  BlockId LookupBlockName(const std::string &name) const;

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

  bool HasBlock(const std::string &name) const;

  BlockId RegisterRuntimeBlock(const BlockDefinition &def,
                               const std::array<std::string, 6> &stems);
  RuntimeOverlayFlushResult FlushRuntimeOverlay();
  /// True while runtime-registered blocks exist (they are not pack content).
  bool HasRuntimeOverlay() const
  {
    return !RuntimeOverlay.empty() || !RuntimeOverlayRemoved.empty();
  }
  void UnregisterRuntimeBlock(const std::string &name);

  void RegisterMissingTextureStems(
      UTextureBaseStorage &out,
      const std::vector<MergedCubeDesc> &descriptors) const;

  void PopulateBlockDefinitionStorage(UBlockDefinitionStorage &out) const;
  void PopulateTextureBaseStorage(UTextureBaseStorage &out) const;

  bool HasBlockId(BlockId id) const;
  const std::string *GetTypeNameById(BlockId id) const
  {
    auto &cached_name=CallingThreadMutableTypeName();
    std::shared_lock lock(CatalogMutex);
    const auto it=IdToName.find(id);
    if(it==IdToName.end())return nullptr;
    cached_name=it->second;return &cached_name;
  }
  // Calling-thread borrowed source;no catalog lookup or publication is made.
  static const std::string &CallingThreadTypeNameForStorage() noexcept {return CallingThreadMutableTypeName();}

  const std::vector<MergedCubeDesc> &GetCubeDescriptors() const
  {
    return CubeDescs;
  }
  const std::unordered_map<std::string, BlockId> &GetNameToId() const
  {
    return NameToId;
  }
  std::unordered_map<std::string, BlockId> SnapshotNameToId() const;

  std::string ComputeCatalogFingerprint() const;

  // Quiescent diagnostic walk; outer read lock also excludes synthetic writes.
  template<class Inspector> void InspectStorage(Inspector &out) const {
    std::shared_lock lock(CatalogMutex);
    out.Vector(ActivePacks);for(const auto &pack:ActivePacks) out.Manifest(pack);
    out.Vector(PrimaryPackIds);for(const auto &name:PrimaryPackIds) out.Text(name);
    out.Text(WorldgenOwnerPackId);out.Map(BlocksByName);out.Map(NameToId);out.Map(IdToName);
    for(const auto &entry:BlocksByName) {
      out.Text(entry.first);out.Definition(entry.second.Definition);out.Text(entry.second.OwnerPackId);
      for(const auto &stem:entry.second.Stems) out.Text(stem);
    }
    for(const auto &entry:NameToId) out.Text(entry.first);
    for(const auto &entry:IdToName) out.Text(entry.second);
    out.Vector(CubeDescs);for(const auto &desc:CubeDescs) out.Descriptor(desc);
    out.Vector(RuntimeOverlay);for(const auto &entry:RuntimeOverlay) {
      out.Definition(entry.first);for(const auto &stem:entry.second) out.Text(stem);
    }
    out.Map(RuntimeOverlayRemoved);for(const auto &name:RuntimeOverlayRemoved) out.Text(name);
    out.Placeholder(PlaceholderCache);
  }

private:
  friend struct RuntimeNameStorageProbe;
  static std::string &CallingThreadMutableTypeName() noexcept {thread_local std::string name;return name;}
  struct PendingBlock
  {
    std::string RegistryName;
    std::string LocalName;
    ResourcePackBlock Block;
    const ResourcePackManifest *Pack{nullptr};
  };
  struct MergedEntry
  {
    BlockDefinition Definition;
    std::array<std::string, 6> Stems{};
    std::string OwnerPackId;
    bool IsQualifiedDuplicate{false};
    // Bounded by current entries;no retained asset-name history after reload.
    std::array<bool,6> PlaceholderWarned{};
  };

  void MergeBlockCatalog(const std::vector<ResourcePackManifest> &sorted);
  void ResolveBlockDefinitions(const std::vector<ResourcePackManifest> &sorted);
  void ResolveTextureAtlas(const std::vector<ResourcePackManifest> &sorted);
  void ApplyTextureOverrides(const std::vector<ResourcePackManifest> &sorted);
  void RegisterTextureSources(UTextureBaseStorage &out) const;

  std::string ResolveStemInPack(const std::string &stem,
                                const ResourcePackManifest &pack,
                                std::unordered_map<std::string,bool> &memo) const;
  void AssignRuntimeIds();
  std::optional<BlockId> FindBoundBlockName(const std::string &name) const;
  BlockId CreateSyntheticBlock(const std::string &name);
  static bool IsPackOwnedEntry(const MergedEntry &entry);

  std::vector<ResourcePackManifest> ActivePacks;
  std::vector<std::string> PrimaryPackIds;
  std::string WorldgenOwnerPackId;
  std::unordered_map<std::string, MergedEntry> BlocksByName;
  std::unordered_map<std::string, BlockId> NameToId;
  size_t LongestBoundNameBytes{0};
  std::unordered_map<BlockId, std::string> IdToName;
  std::vector<MergedCubeDesc> CubeDescs;
  std::vector<std::pair<BlockDefinition, std::array<std::string, 6>>>
      RuntimeOverlay;
  std::unordered_set<std::string> RuntimeOverlayRemoved;
  bool RuntimeOverlayDirty{false};
  std::shared_ptr<UPlaceholderTextureCache> PlaceholderCache;
  int PlaceholderTileSize{16};
  mutable std::shared_mutex CatalogMutex;
  static const std::array<std::string_view,18> kTierAWorldgenNames;
  // Protected by CatalogMutex; one flag per canonical name per catalog epoch.
  std::array<bool,18> TierAMissingLogged{};
};

} // namespace cutum

#endif
