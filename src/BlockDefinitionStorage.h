#ifndef BLOCKDEFINITIONSTORAGE_H
#define BLOCKDEFINITIONSTORAGE_H

#include "BlockDefinition.h"
#include <memory>
#include <unordered_map>

namespace cutum {

class BlockDefinitionStorage {
public:
 void Load(const std::string& modelsPath);
 const BlockDefinition* GetById(BlockId id) const;
 const BlockDefinition* GetByName(const std::string& name) const;
 const std::unordered_map<BlockId, BlockDefinition>& GetAll() const { return byId_; }

private:
 std::unordered_map<BlockId, BlockDefinition> byId_;
 std::unordered_map<std::string, BlockId> nameToId_;
};

} // namespace cutum

#endif
