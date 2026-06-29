#ifndef CREATURE_SKELETAL_GEO_LOADER_H
#define CREATURE_SKELETAL_GEO_LOADER_H

#include "Creatures/Visual/Skeletal/CreatureSkeletalTypes.h"
#include <optional>
#include <string>

namespace cutum
{

class CreatureSkeletalGeoLoader
{
public:
  static std::optional<CreatureSkeletalGeometry>
  LoadFromFile(const std::string &path, const std::string &expectedId = {});
};

} // namespace cutum

#endif
