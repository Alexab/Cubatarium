#ifndef BLOCKREGISTRY_H
#define BLOCKREGISTRY_H

#include <memory>
#include <string>
#include <unordered_map>
#include "BlockTypes.h"

namespace cutum {

class TextureCubeStorage;

class BlockRegistry {
public:
 explicit BlockRegistry(std::shared_ptr<TextureCubeStorage> textures);

 BlockId GetIdByTypeName(const std::string& name) const;
 const std::string& GetTypeNameById(BlockId id) const;
 bool IsSolid(BlockId id) const;
 size_t GetTextureId(BlockId id) const;

private:
 std::shared_ptr<TextureCubeStorage> textures_;
 std::unordered_map<std::string, BlockId> nameToId_;
 std::unordered_map<BlockId, std::string> idToName_;
};

}

#endif
