#ifndef CREATURERIGIDMODELLOADER_H
#define CREATURERIGIDMODELLOADER_H

#include "Creatures/Core/CreatureCatalogTypes.h"
#include <mutex>
#include <string>
#include <unordered_map>

namespace cutum
{

struct VisualAssetStorage;
class VisualAssetCensus;

class CreatureRigidModelCache
{
public:
  static CreatureRigidModelCache &Instance();

  /// Load parts from rigid_model.json relative to species directory.
  std::shared_ptr<const CreatureRigidModelParts>
  LoadParts(const std::string &speciesDir, const std::string &relativePath);

  /// Drops cached rigid models (called when pack overlays reload species).
  void Clear();

  VisualAssetStorage InspectOwnedStorage(VisualAssetCensus *census = nullptr) const;

private:
  CreatureRigidModelCache() = default;

  using PartTable = std::unordered_map<
      std::string, std::shared_ptr<const CreatureRigidModelParts>>;
  mutable std::mutex CacheMutex;
  uint64_t Revision{};
  std::unique_ptr<PartTable> Cache;
};

} // namespace cutum

#endif
