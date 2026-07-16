#ifndef BLOCKDEFINITIONSTORAGE_H
#define BLOCKDEFINITIONSTORAGE_H

#include "Blocks/BlockDefinition.h"
#include <memory>
#include <unordered_map>

namespace cutum
{

struct BlockDefinitionCatalog
{
  std::unordered_map<BlockId, BlockDefinition> ById;
  std::unordered_map<std::string, BlockId> NameToId;
};

class UBlockDefinitionStorage
{
public:
  void Load(const std::string &modelsPath);
  void ReplaceAll(std::unordered_map<BlockId, BlockDefinition> byId,
                  std::unordered_map<std::string, BlockId> nameToId);

  /// Thread-safe RCU snapshot; keep the shared_ptr while reading entries.
  std::shared_ptr<const BlockDefinitionCatalog> GetCatalogSnapshot() const;

  const BlockDefinition *GetById(BlockId Id) const;
  const BlockDefinition *GetByName(const std::string &Name) const;

private:
  // Published via atomic_load/store for cross-thread readers.
  mutable std::shared_ptr<const BlockDefinitionCatalog> Active{
      std::make_shared<BlockDefinitionCatalog>()};
};

} // namespace cutum

#endif
