#ifndef BLOCKDEFINITIONSTORAGE_H
#define BLOCKDEFINITIONSTORAGE_H

#include "Blocks/BlockDefinition.h"
#include <memory>
#include <unordered_map>

namespace cutum
{

class UBlockDefinitionStorage
{
public:
  void Load(const std::string &modelsPath);
  const BlockDefinition *GetById(BlockId Id) const;
  const BlockDefinition *GetByName(const std::string &Name) const;
  const std::unordered_map<BlockId, BlockDefinition> &GetAll() const
  {
    return ById;
  }

private:
  std::unordered_map<BlockId, BlockDefinition> ById;
  std::unordered_map<std::string, BlockId> NameToId;
};

} // namespace cutum

#endif
