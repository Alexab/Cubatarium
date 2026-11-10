#ifndef BLOCKDEFINITIONSTORAGE_H
#define BLOCKDEFINITIONSTORAGE_H

#include "Blocks/BlockDefinition.h"
#include <atomic>
#include <cstdint>
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
  UBlockDefinitionStorage() = default;
  /// Copies share the current immutable catalog; the copy gets its own
  /// revision so per-thread snapshot caches never confuse the two instances.
  UBlockDefinitionStorage(const UBlockDefinitionStorage &other)
      : Active(std::atomic_load_explicit(&other.Active,
                                         std::memory_order_acquire))
  {
  }
  UBlockDefinitionStorage &operator=(const UBlockDefinitionStorage &other)
  {
    if (this != &other)
    {
      Publish(std::atomic_load_explicit(&other.Active,
                                        std::memory_order_acquire));
    }
    return *this;
  }

  void Load(const std::string &modelsPath);
  void ReplaceAll(std::unordered_map<BlockId, BlockDefinition> byId,
                  std::unordered_map<std::string, BlockId> nameToId);

  /// Thread-safe RCU snapshot; keep the shared_ptr while reading entries.
  std::shared_ptr<const BlockDefinitionCatalog> GetCatalogSnapshot() const;

  const BlockDefinition *GetById(BlockId Id) const;
  /// Same lookup via a per-thread snapshot cache (no shared_ptr atomic load).
  /// For per-block hot loops only (chunk commit); GetById keeps the original
  /// per-call snapshot semantics.
  const BlockDefinition *GetByIdFast(BlockId Id) const;
  const BlockDefinition *GetByName(const std::string &Name) const;

private:
  /// Globally unique id of the currently published catalog (changes on every
  /// Load/ReplaceAll) so per-thread snapshot caches can skip the shared_ptr
  /// atomic load on the hot GetById path.
  static uint64_t NextRevision();
  const std::shared_ptr<const BlockDefinitionCatalog> &
  ThreadSnapshot() const;
  void Publish(std::shared_ptr<const BlockDefinitionCatalog> catalog);
  std::atomic<uint64_t> Revision{NextRevision()};
  // Published via atomic_load/store for cross-thread readers.
  mutable std::shared_ptr<const BlockDefinitionCatalog> Active{
      std::make_shared<BlockDefinitionCatalog>()};
};

} // namespace cutum

#endif
