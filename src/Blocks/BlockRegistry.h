#ifndef BLOCKREGISTRY_H
#define BLOCKREGISTRY_H

#include "Blocks/BlockDefinition.h"
#include "World/Math/BlockTypes.h"
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace cutum
{

struct BlockDefinitionCatalog;
class UTextureCubeStorage;
class UBlockDefinitionStorage;
class UBlockMergeRegistry;

class UBlockRegistry
{
public:
  UBlockRegistry(
      std::shared_ptr<UTextureCubeStorage> textures,
      std::shared_ptr<UBlockDefinitionStorage> definitions = nullptr);

  void SetDefinitions(std::shared_ptr<UBlockDefinitionStorage> definitions);
  void SetMergeRegistry(std::shared_ptr<UBlockMergeRegistry> merge_registry);
  void Reload();

  const UBlockDefinitionStorage *GetDefinitions() const
  {
    return Definitions.get();
  }
  std::shared_ptr<UBlockDefinitionStorage> GetDefinitionsShared() const
  {
    return Definitions;
  }
  std::shared_ptr<const BlockDefinitionCatalog>
  GetDefinitionsCatalogSnapshot() const;

  // Opaque owner identity survives address reuse in weak caches; it owns no
  // registry data, so an idle worker never keeps a retired registry alive.
  const std::shared_ptr<const void> &GetLifetimeIdentity() const noexcept {return LifetimeIdentity;}

  BlockId GetIdByTypeName(const std::string &Name) const;
  BlockId GetPackBlockIdByTypeName(const std::string &Name) const;
  const std::string &GetTypeNameById(BlockId Id) const;
  static const std::string &GetThreadNameForCensus();

  bool IsSolid(BlockId Id) const;
  bool BlocksMovement(BlockId Id) const;
  bool IsFallingBlock(BlockId Id) const;
  bool IsLiquid(BlockId Id) const;
  bool IsFloodable(BlockId Id) const;
  bool IsFluidPermeable(BlockId Id) const;
  bool IsFlammable(BlockId Id) const;
  bool IsFireBlock(BlockId Id) const;
  float GetLiquidViscosity(BlockId Id) const;
  bool IsTransparent(BlockId Id) const;
  BlockRenderStyle GetRenderStyle(BlockId Id) const;
  const FluidViewProfile *GetFluidView(BlockId Id) const;
  const BlockPhysicsProfile &Physics(BlockId Id) const;
  const BlockAnimationSpec &Animation(BlockId Id) const;
  int GetLightEmission(BlockId Id) const;
  size_t GetTextureId(BlockId Id) const;
  uint32_t GetBlockAtlasTexture(BlockId Id) const;
  /// True when the block has a merged cube descriptor and loaded GPU texture.
  bool HasRenderableTexture(BlockId Id) const;
  template<class Inspector> void InspectStorage(Inspector &out) const {
    std::shared_lock lock(MapsMutex);
    out.Map(NameToId);out.Map(IdToName);
    for(const auto &entry:NameToId) out.Text(entry.first);
    for(const auto &entry:IdToName) out.Text(entry.second);
    out.BlockStorage(Definitions);out.TextureCubes(Textures);out.Merge(MergeRegistry);
    out.RegistryIdentity(LifetimeIdentity);
  }

private:
  static std::string &ThreadNameKeep();
  const std::shared_ptr<const void> LifetimeIdentity{std::make_shared<const unsigned char>(0)};
  void RebuildMaps();
  std::shared_ptr<UTextureCubeStorage> Textures;
  std::shared_ptr<UBlockDefinitionStorage> Definitions;
  std::shared_ptr<UBlockMergeRegistry> MergeRegistry;
  std::unordered_map<std::string, BlockId> NameToId;
  std::unordered_map<BlockId, std::string> IdToName;
  mutable std::shared_mutex MapsMutex;
  mutable BlockPhysicsProfile SolidDefault;
  BlockAnimationSpec DefaultAnimation{};
};

} // namespace cutum

#endif
