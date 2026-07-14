#ifndef CREATURERIGIDMODELLOADER_H
#define CREATURERIGIDMODELLOADER_H

#include "Creatures/Core/CreatureCatalogTypes.h"
#include <string>
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

private:
  CreatureRigidModelCache() = default;
};

} // namespace cutum

#endif
