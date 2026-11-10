#ifndef CREATURERIGIDMODELLOADER_H
#define CREATURERIGIDMODELLOADER_H

#include "Creatures/Core/CreatureCatalogTypes.h"
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace cutum
{

class CreatureRigidModelCache
{
public:
  static CreatureRigidModelCache &Instance();

  /// Load parts from rigid_model.json relative to species directory.
  bool LoadParts(const std::string &speciesDir, const std::string &relativePath,
                 std::vector<CreatureVisualPartDef> &outParts);

  /// Drops cached rigid models (called when pack overlays reload species).
  void Clear();

private:
  CreatureRigidModelCache() = default;

  std::mutex CacheMutex;
  std::unordered_map<std::string, std::vector<CreatureVisualPartDef>> Cache;
};

} // namespace cutum

#endif
