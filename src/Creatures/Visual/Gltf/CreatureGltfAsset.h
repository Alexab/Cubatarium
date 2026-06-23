#ifndef CREATUREGLTFASSET_H
#define CREATUREGLTFASSET_H

#include "Creatures/Core/CreatureCatalogTypes.h"
#include <string>

namespace cutum
{

/// Runtime placeholder for glTF mesh data (loader deferred — TD-CRE-001).
struct CreatureGltfAsset
{
  CreatureGltfSpec Spec;
  bool Loaded{false};
  std::string SpeciesId;
};

} // namespace cutum

#endif
