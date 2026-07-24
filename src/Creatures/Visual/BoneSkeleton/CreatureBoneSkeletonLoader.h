#ifndef CREATURE_BONE_SKELETON_LOADER_H
#define CREATURE_BONE_SKELETON_LOADER_H

#include "Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonTypes.h"
#include <optional>
#include <string>

namespace cutum
{

class CreatureBoneSkeletonLoader
{
public:
  static std::optional<CreatureBoneSkeletonGeometry>
  LoadFromFile(const std::string &path, const std::string &expectedId = {});
};

} // namespace cutum

#endif
