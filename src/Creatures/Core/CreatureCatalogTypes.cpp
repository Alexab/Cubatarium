#include "Creatures/Core/CreatureCatalogTypes.h"
#include <iostream>

namespace cutum
{

CreatureVisualBackend ParseCreatureVisualBackend(const std::string &s)
{
  if (s == "rigid_voxels")
  {
    return CreatureVisualBackend::RigidVoxels;
  }
  if (s == "gltf_skeleton")
  {
    return CreatureVisualBackend::GltfSkeleton;
  }
  if (!s.empty())
  {
    std::cerr << "ParseCreatureVisualBackend: unknown '" << s
              << "', using rigid_voxels" << std::endl;
  }
  return CreatureVisualBackend::RigidVoxels;
}

const char *ToString(CreatureVisualBackend backend)
{
  switch (backend)
  {
  case CreatureVisualBackend::RigidVoxels:
    return "rigid_voxels";
  case CreatureVisualBackend::GltfSkeleton:
    return "gltf_skeleton";
  }
  return "rigid_voxels";
}

std::string CreatureVisualBackendToString(CreatureVisualBackend backend)
{
  return ToString(backend);
}

} // namespace cutum
