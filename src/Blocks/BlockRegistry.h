#ifndef BLOCKREGISTRY_H
#define BLOCKREGISTRY_H

#include "Blocks/BlockDefinition.h"
#include "World/Math/BlockTypes.h"
#include <memory>
#include <string>
#include <unordered_map>

namespace cutum
{

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

  BlockId GetIdByTypeName(const std::string &Name) const;
  const std::string &GetTypeNameById(BlockId Id) const;

  bool IsSolid(BlockId Id) const;
  bool BlocksMovement(BlockId Id) const;
  bool IsFallingBlock(BlockId Id) const;
  bool IsLiquid(BlockId Id) const;
  bool IsFloodable(BlockId Id) const;
  bool IsFlammable(BlockId Id) const;
  bool IsFireBlock(BlockId Id) const;
  float GetLiquidViscosity(BlockId Id) const;
  bool IsTransparent(BlockId Id) const;
  BlockRenderStyle GetRenderStyle(BlockId Id) const;
  const FluidViewProfile *GetFluidView(BlockId Id) const;
  const BlockPhysicsProfile &Physics(BlockId Id) const;
  const BlockAnimationSpec &Animation(BlockId Id) const;
  size_t GetTextureId(BlockId Id) const;

private:
  void RebuildMaps();
  std::shared_ptr<UTextureCubeStorage> Textures;
  std::shared_ptr<UBlockDefinitionStorage> Definitions;
  std::shared_ptr<UBlockMergeRegistry> MergeRegistry;
  std::unordered_map<std::string, BlockId> NameToId;
  std::unordered_map<BlockId, std::string> IdToName;
  mutable BlockPhysicsProfile SolidDefault;
  BlockAnimationSpec DefaultAnimation{};
};

} // namespace cutum

#endif
