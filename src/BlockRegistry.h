#ifndef BLOCKREGISTRY_H
#define BLOCKREGISTRY_H

#include "BlockDefinition.h"
#include <memory>
#include <string>
#include <unordered_map>
#include "BlockTypes.h"

namespace cutum {

class TextureCubeStorage;
class BlockDefinitionStorage;

class BlockRegistry {
public:
 BlockRegistry(std::shared_ptr<TextureCubeStorage> textures,
               std::shared_ptr<BlockDefinitionStorage> definitions = nullptr);

 void SetDefinitions(std::shared_ptr<BlockDefinitionStorage> definitions);
 void Reload();

 BlockId GetIdByTypeName(const std::string& name) const;
 const std::string& GetTypeNameById(BlockId id) const;

 bool IsSolid(BlockId id) const;
 bool BlocksMovement(BlockId id) const;
 bool IsTransparent(BlockId id) const;
 BlockRenderStyle GetRenderStyle(BlockId id) const;
 const FluidViewProfile* GetFluidView(BlockId id) const;
 const BlockPhysicsProfile& Physics(BlockId id) const;
 size_t GetTextureId(BlockId id) const;

private:
 void RebuildMaps();
 std::shared_ptr<TextureCubeStorage> textures_;
 std::shared_ptr<BlockDefinitionStorage> definitions_;
 std::unordered_map<std::string, BlockId> nameToId_;
 std::unordered_map<BlockId, std::string> idToName_;
 mutable BlockPhysicsProfile solidDefault_;
};

}

#endif
