#ifndef BLOCKMERGEREGISTRY_H
#define BLOCKMERGEREGISTRY_H

#include "Blocks/BlockDefinition.h"
#include "ResourcePacks/PlaceholderTextureCache.h"
#include "ResourcePacks/ResourcePack.h"
#include <array>
#include <memory>
#include <string>
#include <unordered_map>
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

  BlockId RegisterRuntimeBlock(const BlockDefinition &def,
                               const std::array<std::string, 6> &stems);
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

private:
  struct MergedEntry
  {
    BlockDefinition Definition;
    std::array<std::string, 6> Stems{};
  };

  std::string ResolveStem(const std::string &stem,
                          const std::vector<ResourcePackManifest> &packs) const;
  void AssignRuntimeIds();
  BlockId CreateSyntheticBlock(const std::string &name);

  std::vector<ResourcePackManifest> ActivePacks;
  std::unordered_map<std::string, MergedEntry> BlocksByName;
  std::unordered_map<std::string, BlockId> NameToId;
  std::unordered_map<BlockId, std::string> IdToName;
  std::vector<MergedCubeDesc> CubeDescs;
  std::vector<std::pair<BlockDefinition, std::array<std::string, 6>>>
      RuntimeOverlay;
  std::shared_ptr<UPlaceholderTextureCache> PlaceholderCache;
  int PlaceholderTileSize{16};
};

} // namespace cutum

#endif
